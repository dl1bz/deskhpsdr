# Waterfall 3DSS Implementation in deskHPSDR

## Summary
This document describes the necessary changes to implement the Yaesu 3DSS-style 3D waterfall in deskHPSDR, maintaining compatibility with the existing 2D waterfall.

## Created Files

### 1. waterfall3dss.c
Complete 3D OpenGL waterfall implementation with:
- OpenGL 3.3+ rendering using GLSL shaders
- Yaesu-style 3D display with grid and perspective
- Support for multiple color palettes
- Interactive controls (mouse drag for tilt, scroll for zoom)
- Spectrum history management with 120 lines of depth
- Support for pan/zoom and frequency changes

### 2. waterfall3dss.h
Header file with public function declarations:
- `waterfall3dss_init()` - Initializes the 3D waterfall
- `waterfall3dss_update()` - Updates with new spectrum data
- OpenGL callbacks (realize, unrealize, render)

## Required Changes

### 1. receiver.h
Add field for waterfall mode selection:

```c
typedef struct _receiver {
  // ... existing fields ...
  
  int waterfall_low;
  int waterfall_high;
  int waterfall_automatic;
  
  // NEW: Add this field
  int waterfall_mode;  // 0 = 2D (Cairo), 1 = 3DSS (OpenGL)
  
  cairo_surface_t *panadapter_surface;
  GdkPixbuf *pixbuf;
  // ... rest of fields ...
} RECEIVER;
```

### 2. receiver.c
Add initialization and mode switching:

```c
#include "waterfall.h"
#include "waterfall3dss.h"  // NEW

RECEIVER *rx_create_receiver(int id, int pixels, int width, int height) {
  // ... existing code ...
  
  rx->waterfall_low = -100;
  rx->waterfall_high = -40;
  rx->waterfall_automatic = 0;
  rx->waterfall_mode = 0;  // NEW: Initialize in 2D mode by default
  
  // ... rest of initialization ...
}

void rx_reconfigure(RECEIVER *rx, int height) {
  // ... existing code ...
  
  if(rx->display_waterfall) {
    if(rx->waterfall==NULL) {
      // MODIFIED: Choose between 2D and 3DSS
      if (rx->waterfall_mode == 1) {
        waterfall3dss_init(rx, rx->width, myheight);
      } else {
        waterfall_init(rx, rx->width, myheight);
      }
      gtk_fixed_put(GTK_FIXED(rx->panel), rx->waterfall, 0, y);
    }
    // ... rest of code ...
  }
}
```

### 3. rx_panadapter.c (or wherever waterfall_update is called)
Modify to call the appropriate function:

```c
static gint update_display(gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;
  
  if (rx->displaying) {
    if (rx->pixels > 0) {
      g_mutex_lock(&rx->display_mutex);
      GetPixels(rx->id, 0, rx->pixel_samples, &rc);
      if (rc) {
        if (rx->display_panadapter) {
          rx_panadapter_update(rx);
        }
        if (rx->display_waterfall) {
          // MODIFIED: Call appropriate function
          if (rx->waterfall_mode == 1) {
            waterfall3dss_update(rx);
          } else {
            waterfall_update(rx);
          }
        }
      }
      g_mutex_unlock(&rx->display_mutex);
    }
  }
  return TRUE;
}
```

### 4. display_menu.c (or configuration menu)
Add option to toggle between 2D and 3DSS:

```c
static void waterfall_mode_cb(GtkWidget *widget, gpointer data) {
  RECEIVER *rx = (RECEIVER *)data;
  int new_mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)) ? 1 : 0;
  
  if (rx->waterfall_mode != new_mode) {
    rx->waterfall_mode = new_mode;
    
    // Remove current waterfall
    if (rx->waterfall != NULL) {
      gtk_container_remove(GTK_CONTAINER(rx->panel), rx->waterfall);
      rx->waterfall = NULL;
    }
    
    // Recreate with new mode
    rx_reconfigure(rx, rx->height);
  }
}

// In display menu:
GtkWidget *waterfall_mode_switch = gtk_check_button_new_with_label("Waterfall 3DSS");
gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(waterfall_mode_switch), 
                              active_receiver->waterfall_mode);
g_signal_connect(waterfall_mode_switch, "toggled", G_CALLBACK(waterfall_mode_cb), 
                 active_receiver);
```

### 5. property.c (save/restore settings)
Add support for saving waterfall mode:

```c
void rx_save_state(const RECEIVER *rx) {
  // ... existing code ...
  
  sprintf(name, "receiver.%d.waterfall_mode", rx->id);
  sprintf(value, "%d", rx->waterfall_mode);
  setProperty(name, value);
  
  // ... rest of code ...
}

void rx_restore_state(RECEIVER *rx) {
  // ... existing code ...
  
  sprintf(name, "receiver.%d.waterfall_mode", rx->id);
  value = getProperty(name);
  if (value) rx->waterfall_mode = atoi(value);
  
  // ... rest of code ...
}
```

### 6. Makefile
Add waterfall3dss to files to compile:

```makefile
OBJS = main.o \
       receiver.o \
       waterfall.o \
       waterfall3dss.o \
       # ... other files ...
```

## Dependencies

### Required libraries (already present in deskhpsdr):
- GTK+3 with GtkGLArea support
- OpenGL 3.3+ (via libepoxy)
- Cairo (for 2D waterfall)

### Compilation:
```bash
gcc -c waterfall3dss.c `pkg-config --cflags gtk+-3.0 epoxy`
```

## Interactive Controls (3DSS mode only)

### Mouse:
- **Drag (left button)**: Adjusts 3D display tilt
- **Scroll**: Adjusts zoom (camera distance)

### Adjustable parameters:
- `WATERFALL_DEPTH`: Number of history lines (default: 120)
- `WATERFALL_Z_SPAN`: Display depth in 3D units (default: 1.60)
- Tilt angle: 0.0 to 5.0 (adjustable via mouse)
- Zoom level: 1.0 to 4.0 (adjustable via scroll)

## Differences between 2D and 3DSS

### 2D Waterfall (Cairo - current):
- ✅ Fast and lightweight rendering
- ✅ Works on any hardware
- ✅ Traditional flat display
- ❌ No 3D perspective
- ❌ No interactive viewing controls

### 3DSS Waterfall (OpenGL - new):
- ✅ Yaesu FT-dx10 style 3D display
- ✅ Visual perspective and depth
- ✅ Interactive controls (tilt/zoom)
- ✅ 3D grid for reference
- ✅ Advanced color palettes
- ⚠️ Requires OpenGL 3.3+
- ⚠️ Higher GPU usage

## Data Flow

```
WDSP/Radio → rx->pixel_samples[] 
           ↓
waterfall_mode == 0 → waterfall_update() → Cairo 2D
           ↓
waterfall_mode == 1 → waterfall3dss_update() → OpenGL 3D
           ↓
          Display in GTK widget (waterfall)
```

## Testing

1. **Compile** with the new changes
2. **Start** deskHPSDR normally (2D mode by default)
3. **Display Menu** → Select "Waterfall 3DSS"
4. **Verify** 3D rendering
5. **Test** controls (drag/scroll)
6. **Switch** back to 2D

## Compatibility

- ✅ Maintains 100% compatibility with existing 2D waterfall
- ✅ Does not break existing functionality
- ✅ 2D mode remains default
- ✅ Users can freely choose between modes
- ✅ Configuration automatically saved/restored

## Implementation Notes

1. 3D waterfall uses GtkGLArea (GTK3), no need to create separate OpenGL window
2. Spectrum history is managed in circular buffer (ring buffer)
3. Frequency rotation (VFO changes) is handled by shifting history horizontally
4. AGC stabilization: first 5 updates are ignored to avoid artifacts
5. Mutex (`display_mutex`) protects concurrent access to spectrum data

## Next Steps

1. ✅ Create waterfall3dss.c and waterfall3dss.h
2. ⬜ Modify receiver.h to add `waterfall_mode`
3. ⬜ Update receiver.c to support both modes
4. ⬜ Add toggle in display menu
5. ⬜ Implement save/restore of mode
6. ⬜ Update Makefile
7. ⬜ Test and adjust visual parameters
8. ⬜ Document in project README

## Conclusion

The waterfall 3DSS implementation in deskHPSDR maintains full compatibility with existing code, offering users the choice between:
- Traditional 2D waterfall (lightweight, compatible with any hardware)
- Modern 3DSS waterfall (enhanced visuals, Yaesu style, interactive controls)

Users can switch between modes at any time through the display menu.
