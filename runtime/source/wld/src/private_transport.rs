//! Linux clients authenticate the connected peer, rather than trusting a replayable HTTP echo.
use std::{
    io::{self, Read, Write},
    net::{Shutdown, SocketAddr, TcpStream},
    path::Path,
    time::Duration,
};

pub enum Stream {
    Tcp(TcpStream),
    #[cfg(unix)]
    Unix(std::os::unix::net::UnixStream),
}
impl From<TcpStream> for Stream {
    fn from(stream: TcpStream) -> Self {
        Self::Tcp(stream)
    }
}
impl Read for Stream {
    fn read(&mut self, bytes: &mut [u8]) -> io::Result<usize> {
        match self {
            Self::Tcp(s) => s.read(bytes),
            #[cfg(unix)]
            Self::Unix(s) => s.read(bytes),
        }
    }
}
impl Write for Stream {
    fn write(&mut self, bytes: &[u8]) -> io::Result<usize> {
        match self {
            Self::Tcp(s) => s.write(bytes),
            #[cfg(unix)]
            Self::Unix(s) => s.write(bytes),
        }
    }
    fn flush(&mut self) -> io::Result<()> {
        match self {
            Self::Tcp(s) => s.flush(),
            #[cfg(unix)]
            Self::Unix(s) => s.flush(),
        }
    }
}
impl Stream {
    pub fn set_nonblocking(&self, enabled: bool) -> io::Result<()> {
        match self {
            Self::Tcp(s) => s.set_nonblocking(enabled),
            #[cfg(unix)]
            Self::Unix(s) => s.set_nonblocking(enabled),
        }
    }
    pub fn set_read_timeout(&self, timeout: Option<Duration>) -> io::Result<()> {
        match self {
            Self::Tcp(s) => s.set_read_timeout(timeout),
            #[cfg(unix)]
            Self::Unix(s) => s.set_read_timeout(timeout),
        }
    }
    pub fn set_write_timeout(&self, timeout: Option<Duration>) -> io::Result<()> {
        match self {
            Self::Tcp(s) => s.set_write_timeout(timeout),
            #[cfg(unix)]
            Self::Unix(s) => s.set_write_timeout(timeout),
        }
    }
    pub fn shutdown(&self, how: Shutdown) -> io::Result<()> {
        match self {
            Self::Tcp(s) => s.shutdown(how),
            #[cfg(unix)]
            Self::Unix(s) => s.shutdown(how),
        }
    }
}

pub fn connect(address: SocketAddr, nonce: Option<&Path>) -> io::Result<Stream> {
    #[cfg(unix)]
    {
        let _ = address;
        let path = nonce
            .ok_or_else(|| denied("Private endpoint unconfigured; key withheld"))?
            .with_extension("sock");
        private_parent(&path)?;
        let meta = std::fs::symlink_metadata(&path)?;
        use std::os::unix::fs::{FileTypeExt, MetadataExt};
        if !meta.file_type().is_socket()
            || meta.uid() != unsafe { libc::geteuid() }
            || meta.mode() & 0o077 != 0
        {
            return Err(denied("Private endpoint identity unverified; key withheld"));
        }
        let stream = unix_connect(&path)?;
        verify_peer(&stream)?;
        Ok(Stream::Unix(stream))
    }
    #[cfg(not(unix))]
    {
        let _ = nonce;
        TcpStream::connect_timeout(&address, Duration::from_secs(2)).map(Stream::Tcp)
    }
}

#[cfg(unix)]
fn denied(message: &str) -> io::Error {
    io::Error::new(io::ErrorKind::PermissionDenied, message)
}

// A saturated local backlog must not hang startup or a status probe indefinitely.
#[cfg(target_os = "linux")]
fn unix_connect(path: &Path) -> io::Result<std::os::unix::net::UnixStream> {
    use std::os::{
        fd::{AsRawFd, FromRawFd, OwnedFd},
        unix::ffi::OsStrExt,
    };
    let mut address: libc::sockaddr_un = unsafe { std::mem::zeroed() };
    address.sun_family = libc::AF_UNIX as libc::sa_family_t;
    let bytes = path.as_os_str().as_bytes();
    if bytes.len() >= address.sun_path.len() || bytes.contains(&0) {
        return Err(io::Error::new(
            io::ErrorKind::InvalidInput,
            "Invalid or oversized private endpoint path",
        ));
    }
    for (to, from) in address.sun_path.iter_mut().zip(bytes) {
        *to = *from as libc::c_char;
    }
    let raw = unsafe {
        libc::socket(
            libc::AF_UNIX,
            libc::SOCK_STREAM | libc::SOCK_CLOEXEC | libc::SOCK_NONBLOCK,
            0,
        )
    };
    if raw == -1 {
        return Err(io::Error::last_os_error());
    }
    let fd = unsafe { OwnedFd::from_raw_fd(raw) };
    let result = unsafe {
        libc::connect(
            fd.as_raw_fd(),
            (&address as *const libc::sockaddr_un).cast(),
            std::mem::size_of_val(&address) as libc::socklen_t,
        )
    };
    if result == -1 {
        let error = io::Error::last_os_error();
        if error.raw_os_error() != Some(libc::EINPROGRESS) {
            return Err(error);
        }
        let mut poll = libc::pollfd {
            fd: fd.as_raw_fd(),
            events: libc::POLLOUT,
            revents: 0,
        };
        let ready = unsafe { libc::poll(&mut poll, 1, 2000) };
        if ready == 0 {
            return Err(io::Error::new(
                io::ErrorKind::TimedOut,
                "Private endpoint connect timeout",
            ));
        }
        if ready == -1 {
            return Err(io::Error::last_os_error());
        }
        let mut error: libc::c_int = 0;
        let mut length = std::mem::size_of_val(&error) as libc::socklen_t;
        if unsafe {
            libc::getsockopt(
                fd.as_raw_fd(),
                libc::SOL_SOCKET,
                libc::SO_ERROR,
                (&mut error as *mut libc::c_int).cast(),
                &mut length,
            )
        } == -1
        {
            return Err(io::Error::last_os_error());
        }
        if error != 0 {
            return Err(io::Error::from_raw_os_error(error));
        }
    }
    let stream = std::os::unix::net::UnixStream::from(fd);
    stream.set_nonblocking(false)?;
    Ok(stream)
}
#[cfg(all(unix, not(target_os = "linux")))]
fn unix_connect(path: &Path) -> io::Result<std::os::unix::net::UnixStream> {
    std::os::unix::net::UnixStream::connect(path)
}

#[cfg(unix)]
fn private_parent(path: &Path) -> io::Result<()> {
    use std::os::unix::fs::MetadataExt;
    let parent = path
        .parent()
        .ok_or_else(|| denied("Private endpoint has no parent"))?;
    let meta = std::fs::symlink_metadata(parent)?;
    if !meta.is_dir() || meta.uid() != unsafe { libc::geteuid() } || meta.mode() & 0o077 != 0 {
        return Err(denied(
            "Private endpoint directory must be owner-only (chmod 700); key withheld",
        ));
    }
    Ok(())
}

#[cfg(target_os = "linux")]
pub fn verify_peer(stream: &std::os::unix::net::UnixStream) -> io::Result<()> {
    use std::os::fd::AsRawFd;
    let mut credentials: libc::ucred = unsafe { std::mem::zeroed() };
    let mut length = std::mem::size_of_val(&credentials) as libc::socklen_t;
    let result = unsafe {
        libc::getsockopt(
            stream.as_raw_fd(),
            libc::SOL_SOCKET,
            libc::SO_PEERCRED,
            (&mut credentials as *mut libc::ucred).cast(),
            &mut length,
        )
    };
    if result != 0 {
        return Err(io::Error::last_os_error());
    }
    if length as usize != std::mem::size_of_val(&credentials)
        || credentials.uid != unsafe { libc::geteuid() }
    {
        return Err(denied("Private endpoint peer UID differs; key withheld"));
    }
    Ok(())
}
#[cfg(all(unix, not(target_os = "linux")))]
pub fn verify_peer(_: &std::os::unix::net::UnixStream) -> io::Result<()> {
    Err(denied("This platform has no configured peer-UID verifier"))
}

#[cfg(unix)]
pub struct PrivateListener {
    listener: std::os::unix::net::UnixListener,
    path: std::path::PathBuf,
    identity: (u64, u64),
}
#[cfg(unix)]
impl PrivateListener {
    pub fn bind(nonce: &Path) -> io::Result<Self> {
        use std::os::unix::fs::{DirBuilderExt, FileTypeExt, MetadataExt, PermissionsExt};
        let path = nonce.with_extension("sock");
        let parent = path
            .parent()
            .ok_or_else(|| denied("Private endpoint has no parent"))?;
        std::fs::DirBuilder::new()
            .recursive(true)
            .mode(0o700)
            .create(parent)?;
        private_parent(&path)?;
        match std::fs::symlink_metadata(&path) {
            Ok(meta) => {
                if !meta.file_type().is_socket() || meta.uid() != unsafe { libc::geteuid() } {
                    return Err(denied("Refusing to replace a foreign private endpoint"));
                }
                match unix_connect(&path) {
                    Ok(_) => {
                        return Err(io::Error::new(
                            io::ErrorKind::AddrInUse,
                            "Private endpoint already running",
                        ));
                    }
                    Err(e) if e.kind() == io::ErrorKind::ConnectionRefused => {
                        let current = std::fs::symlink_metadata(&path)?;
                        if (current.dev(), current.ino()) != (meta.dev(), meta.ino()) {
                            return Err(denied("Private endpoint changed during startup"));
                        }
                        std::fs::remove_file(&path)?;
                    }
                    Err(e) => return Err(e),
                }
            }
            Err(e) if e.kind() == io::ErrorKind::NotFound => (),
            Err(e) => return Err(e),
        }
        let listener = std::os::unix::net::UnixListener::bind(&path)?;
        let meta = std::fs::symlink_metadata(&path)?;
        let bound = Self {
            listener,
            path,
            identity: (meta.dev(), meta.ino()),
        };
        std::fs::set_permissions(&bound.path, std::fs::Permissions::from_mode(0o600))?;
        bound.listener.set_nonblocking(true)?;
        Ok(bound)
    }
    pub fn accept(&self) -> io::Result<Stream> {
        let (stream, _) = self.listener.accept()?;
        verify_peer(&stream)?;
        Ok(Stream::Unix(stream))
    }
}
#[cfg(unix)]
impl Drop for PrivateListener {
    fn drop(&mut self) {
        use std::os::unix::fs::MetadataExt;
        if std::fs::symlink_metadata(&self.path).is_ok_and(|m| (m.dev(), m.ino()) == self.identity)
        {
            let _ = std::fs::remove_file(&self.path);
        }
    }
}
