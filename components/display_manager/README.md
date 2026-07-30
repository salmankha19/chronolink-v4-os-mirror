# Display Manager

## Purpose
Zero-malloc JSON command executor for declarative UI frames.

## JSON Examples
```json
{"cmd":"clear", "color":"#000000"}
{"cmd":"fill_rect", "x":0, "y":0, "w":100, "h":60, "color":"#FF0000"}
{"cmd":"draw_line", "x0":0, "y0":0, "x1":100, "y1":100, "color":"#FFFFFF"}
{"cmd":"show"}
```

## Memory
Static queue with 8 slots x 512 bytes each (~4 KB payload), no heap allocation in the execution hot path.

## Integration
Runs on Core 1 through core1_task. Any task can submit JSON commands using display_manager_submit_json().
