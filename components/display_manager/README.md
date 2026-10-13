# Display Manager

## Purpose
Zero-malloc declarative frame executor. Producers build lightweight dm_frame_t
descriptors and submit them to a lock-free queue; the consumer (Core 1 /
core1_task) executes them via HAL_GFX.

## Memory
Static queue with 8 slots x 512 bytes each (~4 KB payload), no heap allocation in the execution hot path.

## Integration
Runs on Core 1 through core1_task. Any task can submit frames using display_manager_submit_frame().
