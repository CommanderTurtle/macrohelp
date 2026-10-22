Table of Contents:
------------------

- Adding and Deleting Tasks - 10
- Controlling Omarchy - 
- Code Broken? - Run ./wld/tests/qml_check_basics.py & _semantics.py



## Adding and Deleting Tasks

The omarsket engine supports:

1. **`save`** — creates/updates a task: `{"method":"save","task":task,"create":true}`
2. **`delete`** — deletes a task: `{"method":"delete","name":id}`
3. **`snapshot`** — gets current state (includes task list)

And the wld httpd's `Runner` has:
- `self.executor.call(json!({"method":"save","task":task,"create":true}))` — create
- `self.executor.call(json!({"method":"delete","name":id}))` — delete
- `self.snapshot()` — check if task exists

Summary: macrohelp's' wld httpd has clean methods for creation and deletion of tasks:

**Create**: `executor.call(json!({"method":"save","task":task,"create":true}))`
**Delete**: `executor.call(json!({"method":"delete","name":id}))`
**Check existence**: `snapshot()` → check if task id is in the tasks array

Example of a startup default task (something that always must exist to be later called):
1. In `main.rs`, after creating the `App` and before starting the server:
   - Call `app.runner.snapshot()` to get the current task list
   - Check if a "Blink to * (center bottom)" task exists
   - If not, call `app.runner.executor.call(json!({"method":"save","task":blink_task,"create":true}))`
2. On shutdown (after `serve()` returns):
   - Call `app.runner.executor.call(json!({"method":"delete","name":blink_task_id}))`

This is clean because:
- It uses the existing executor architecture
- No file I/O in the httpd (the omarsket engine handles it)
- The create/delete methods are already tested and working
- It's a single point of control in `main.rs`
