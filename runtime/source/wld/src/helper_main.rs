fn main() {
    if let Err(e) = macrohelp_overlay::overlay::run() {
        eprintln!("{e}");
        std::process::exit(1)
    }
}
