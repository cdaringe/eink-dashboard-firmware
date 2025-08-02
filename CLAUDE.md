# E-Ink Dashboard Firmware - Development Notes

## Performance Requirements

### Display Updates
- **IMPORTANT**: `partialUpdate()` does NOT work on this Inkplate model
- All display updates must use `display()` which takes ~2-3 seconds
- This means button handling must be robust against display blocking
- Menu updates will be inherently slow due to hardware limitations

### WiFi Optimization
- **CRITICAL**: WiFi connection is slow (~3-5 seconds)
- Only initialize WiFi when absolutely necessary:
  - When fetching images from web
  - NOT for menu display or navigation
- WiFi flow: Off → Connect only when needed → Off after use → Deep sleep

### Button Responsiveness
- Button polling delay: 20ms maximum for responsive input
- State machine must handle edge cases without freezing
- Proper state reset after menu cycling

## Architecture Notes

### Menu System
- Show all options with moving pointer (not single-item display)
- Pointer column width: ~40px for efficient partial updates
- Menu selection wraps around (0 → max → 0)

### Power Management
- Deep sleep between operations
- ESP32 wakeup sources: Timer + GPIO button
- WiFi off during sleep

## Code Standards
- Use descriptive variable names (`wake_button_pressed_curr` vs `button_state`)
- Clear state machine comments for button handling
- Consistent naming patterns throughout
