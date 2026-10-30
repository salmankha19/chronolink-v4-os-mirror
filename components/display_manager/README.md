# Display Manager

## Purpose
Zero-malloc frame executor. Producers build lightweight dm_frame_t command
lists and submit them to a mutex-guarded queue; the consumer (Core 1 /
core1_task) executes them via HAL_GFX through display_manager_process_frame().

## Memory
3 slots (`DM_FRAME_QUEUE_DEPTH`), each `sizeof(dm_frame_t)` bytes -- about
2.1 KB in total at `DM_MAX_TEXT_LEN` 32. The queue and the submit mutex are
created by `xQueueCreate()` / `xSemaphoreCreateMutex()` in
`display_manager_init()`; there is no heap allocation in the execution hot
path and none after init.

## Integration
Runs on Core 1 through core1_task. Any task can submit frames using
display_manager_submit_frame(); submits are serialised by a mutex (5 ms mutex
timeout, 5 ms queue timeout), and a frame that cannot be queued is dropped
with a warning.

## Statistics
display_manager_frames_dispatched() returns the number of frames dequeued and
executed since boot. A TEMPORARY ESP_LOGI in display_manager_process_frame()
prints it every 5th frame until the panel is verified on real hardware.
