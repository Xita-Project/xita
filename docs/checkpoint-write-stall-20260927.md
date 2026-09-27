# Checkpoint write stalls — 2026-09-27

Perf272 measured two 3,428,352-byte savegame.bin transfers at 199,353 and
191,268 microseconds. The latter occurred during the movement capture containing
a 264,073 us frame (263,887 us before Present). Transfer records lack a frame ID,
so their exact correspondence remains unproven. This is separate from sustained
pod/firing frame costs; removing an occasional save stall would not establish
20 FPS gameplay.

Source audit:

- xk_NtWriteFile calls file_guest_io synchronously, then updates offset/IOSB,
  signals an event and queues completion APCs. No pending I/O operation exists.
- file_guest_io already merges physically adjacent guest pages into one request;
  it is not unconditionally issuing a write for every 4 KB page.
- xk_os_write uses positional sceIoPwrite. fd_acquire updates an unprotected
  process-wide 16-descriptor LRU; another acquire may close its tail descriptor.
- xk_file_release can close/free a file and perform delete-on-close. Retaining
  only a guest handle or a borrowed memory pointer on another thread is unsafe.
- xk_wait_u32 supports cooperative owner-fiber waits, but yielding allows other
  guest threads to mutate mappings/files. The native scene helper instead polls
  without entering the guest scheduler. These are different ownership contexts.

Therefore simply placing xk_os_write on a worker is not a correct optimization.
A candidate needs owned input bytes, a pinned descriptor/object lifetime, ordered
same-file read/write/flush/close handling and owner-side offset/IOSB/event/APC
publication after actual completion. It must propagate short writes/errors and
keep existing persistence guarantees. First capture the calling guest thread
and completion usage to establish whether other guest work can overlap the save;
a blocked main simulation would limit frame benefits even with a native worker.
No asynchronous save implementation or change to user saves was made.
