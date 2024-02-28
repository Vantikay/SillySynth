#define MINIAUDIO_IMPLEMENTATION

#include <gtk/gtk.h>
#include <stack>
#include "miniaudio.h"
#include "glib/gprintf.h"

#define DEVICE_FORMAT       ma_format_f32
#define SAMPLE_FORMAT       ma_format_f32
#define DEVICE_CHANNELS     1
#define DEVICE_SAMPLE_RATE  48000
#define EXPORT_FORMAT       ma_format_f32
#define EXPORT_CHANNELS     1
#define EXPORT_SAMPLE_RATE  48000

using namespace std;

// g++ $( pkg-config --cflags gtk4 ) -o silly_synth silly_synth.cpp $( pkg-config --libs gtk4 ) -ldl -lm -lpthread

int playbackX = 0;

bool** notes;

int pianoKeyCount = 25; // two octaves + extra C
int pianoGridWidth = 32; // arbitrary for now before scrubbing is implemented
int baseKeyNote = 48; // C3
int pianoRollBorder = 100;
double tempo = 8.0; // TODO (this is in grid spaces per second)
int previousFrameTime;



bool editNoteSoundActive = false;
int editX = 0;
int editY = 0;


// https://newt.phys.unsw.edu.au/jw/notes.html
double pitch_from_note(int note)
{
  return std::exp2((note - 69) / 12.0) * 440.0;
}



double scrubberPosition = 0.0;
int scrubberWidth = 5;
int scrubberHeightOffset = 20;
double playbackTime = 0.0; // in seconds I think
bool playing = false;
bool exporting = false;

ma_device device;



float microseconds_to_seconds(int ms)
{
  return ms * 0.000001f;
}

static void start_playback(GtkWidget* widget, gpointer data)
{
  playing = true;  
  g_print("now playing!\n");
  previousFrameTime = -1;
  playbackX = 0;
}

static void stop_playback(GtkWidget* widget, gpointer data)
{
  playing = false;  
  g_print("stopped playing\n");
  gtk_widget_queue_draw(GTK_WIDGET(data));  
}

static void reset_playback(GtkWidget* widget, gpointer data)
{
  playing = false;
  playbackTime = 0.0;
  scrubberPosition = 0.0;
  gtk_widget_queue_draw(GTK_WIDGET(data));  
  g_print("reset\n");
}

static void edit_note(GtkWidget* widget)
{
  
}

bool get_note(int x, int y)
{
  if (x < 0 || x >= pianoGridWidth || y < 0 || y >= pianoKeyCount)
  {
    return false;
  }
  return notes[x][y];
}

void set_note(int x, int y, bool value)
{
  if (x < 0 || x >= pianoGridWidth || y < 0 || y >= pianoKeyCount)
  {
    return;
  }
  notes[x][y] = value;
}

void toggle_note(int x, int y)
{
  if (x < 0 || x >= pianoGridWidth || y < 0 || y >= pianoKeyCount)
  {
    return;
  }
  notes[x][y] = !notes[x][y];
  // g_print("note toggled\n");
}

enum ActionType
{
  TOGGLE_NOTE,
  ADD_NOTE,
  REMOVE_NOTE,
  CLEAR_NOTES
};

struct Action
{
  ActionType type;
  int data1;
  int data2;
};



stack<Action> undoStack;
stack<Action> redoStack;
bool hasSavedNotes = false;
bool** savedNotes; // allows for ONE undo of a clear action

static void clear_redo_stack()
{
  redoStack = stack<Action>();
}

static void undo(GtkWidget* widget, gpointer data)
{
  if (undoStack.empty())
  {
    return;
  }

  Action a = undoStack.top();
  undoStack.pop();
  redoStack.push(a);

  // perform undo operation
  switch (a.type)
  {
    case TOGGLE_NOTE:
    {
      toggle_note(a.data1, a.data2);
      break;
    }
    case ADD_NOTE:
    {
      set_note(a.data1, a.data2, false);
      break;
    }
    case REMOVE_NOTE:
    {
      set_note(a.data1, a.data2, true);
      break;
    }
    case CLEAR_NOTES:
    {
      if (hasSavedNotes)
      {
        for (int i = 0; i < pianoGridWidth; i++)
        {
          for (int j = 0; j < pianoKeyCount; j++)
          {
            set_note(i, j, savedNotes[i][j]);
          }
        }
        hasSavedNotes = false;
      }
      else
      {
        undoStack = stack<Action>(); // clear undo stack, since we dont know what was in the roll befor the clear
      }
      break;
    }
  }
  gtk_widget_queue_draw(GTK_WIDGET(data));  
}

static void redo(GtkWidget* widger, gpointer data)
{
  if (redoStack.empty())
  {
    return;
  }

  Action a = redoStack.top();
  redoStack.pop();
  undoStack.push(a);

  // perform undo operation
  switch (a.type)
  {
    case TOGGLE_NOTE:
    {
      toggle_note(a.data1, a.data2);
      break;
    }
    case ADD_NOTE:
    {
      set_note(a.data1, a.data2, true);
      break;
    }
    case REMOVE_NOTE:
    {
      set_note(a.data1, a.data2, false);
      break;
    }
    case CLEAR_NOTES:
    {
      for (int i = 0; i < pianoGridWidth; i++)
      {
        for (int j = 0; j < pianoKeyCount; j++)
        {
          savedNotes[i][j] = get_note(i,j);
          set_note(i, j, false);
        }
      }
      hasSavedNotes = true;
    }
  }
  gtk_widget_queue_draw(GTK_WIDGET(data));  
}

gboolean handle_undo_shortcut(GtkWidget* widget, GVariant* args, gpointer user_data)
{
  undo(widget, NULL);
  return true;
}

void init_notes()
{
  notes = new bool*[pianoGridWidth];
  savedNotes = new bool*[pianoGridWidth];
  for (int i = 0; i < pianoGridWidth; i++)
  {
    notes[i] = new bool[pianoKeyCount];
    savedNotes[i] = new bool[pianoKeyCount];
    for (int j = 0; j < pianoKeyCount; j++)
    {
      notes[i][j] = false;
      savedNotes[i][j] = false;
    }
  }
}

void delete_notes()
{
  for (int i = 0; i < pianoGridWidth; i++)
  {
    delete notes[i];
    delete savedNotes[i];
  }
  delete notes;
  delete savedNotes;
}



static void clear_notes(GtkWidget* widget, gpointer data)
{
  for (int i = 0; i < pianoGridWidth; i++)
  {
    for (int j = 0; j < pianoGridWidth; j++)
    {
      savedNotes[i][j] = get_note(i,j);
      set_note(i, j, false);
    }
  }
  hasSavedNotes = true;

  Action clearAction;
  clearAction.type = ActionType::CLEAR_NOTES;
  undoStack.push(clearAction);
  clear_redo_stack();


  gtk_widget_queue_draw(GTK_WIDGET(data));  
}


double dragStartX = 0.0;
double dragStartY = 0.0;

static void piano_roll_primary_drag_begin(GtkGestureDrag *gesture,
            double          x,
            double          y,
            GtkWidget      *area)
{
  // g_print("drag begin\n");
  dragStartX = x;
  dragStartY = y;
  int width = gtk_widget_get_allocated_width(area);
  int height = gtk_widget_get_allocated_height(area);
  if (x >= pianoRollBorder && x < width - pianoRollBorder
      && y >= pianoRollBorder && y < height - pianoRollBorder)
  {
    double keyWidth = (double) (width - 2 * pianoRollBorder) / pianoGridWidth;
    double xd = (x - pianoRollBorder) / keyWidth;
    double keyHeight = (double) (height - 2 * pianoRollBorder) / pianoKeyCount;
    double yd = (height - y - pianoRollBorder) / keyHeight;
    // g_printf("xd: %f, yd: %f\n", xd, yd);
    toggle_note((int) xd, (int) yd);

    Action toggleAction;
    toggleAction.type = ActionType::TOGGLE_NOTE;
    toggleAction.data1 = (int) xd;
    toggleAction.data2 = (int) yd;
    undoStack.push(toggleAction);
    clear_redo_stack();

    editX = (int) xd;
    editY = (int) yd;
    editNoteSoundActive = true;
    gtk_widget_queue_draw(area);
  }
}

static void piano_roll_primary_drag_update (GtkGestureClick *gesture,
         double           x,
         double           y,
         GtkWidget       *area)
{
  // g_print("drag update\n");
  x += dragStartX;
  y += dragStartY;
  int width = gtk_widget_get_allocated_width(area);
  int height = gtk_widget_get_allocated_height(area);
  if (x >= pianoRollBorder && x < width - pianoRollBorder
      && y >= pianoRollBorder && y < height - pianoRollBorder)
  {
    double keyWidth = (double) (width - 2 * pianoRollBorder) / pianoGridWidth;
    double xd = (x - pianoRollBorder) / keyWidth;
    double keyHeight = (double) (height - 2 * pianoRollBorder) / pianoKeyCount;
    double yd = (height - y - pianoRollBorder) / keyHeight;
    if ((int) xd != editX || (int) yd != editY )
    {
      // g_printf("xd: %f, yd: %f\n", xd, yd);
      editX = (int) xd;
      editY = (int) yd;
      toggle_note((int) xd, (int) yd);

      Action toggleAction;
      toggleAction.type = ActionType::TOGGLE_NOTE;
      toggleAction.data1 = (int) xd;
      toggleAction.data2 = (int) yd;
      undoStack.push(toggleAction);
      clear_redo_stack();

      gtk_widget_queue_draw(area);
    }
  }
}

static void piano_roll_primary_drag_end (GtkGestureClick *gesture,
         double           x,
         double           y,
         GtkWidget       *area)
{
  // g_print("drag end\n");
  editNoteSoundActive = false;
}

static gboolean animate_piano_roll(GtkWidget* widget, GdkFrameClock* frame_clock, gpointer user_data)
{
  // g_print("animation called\n");
  if (!playing)
  {
    return true;
  }

  if (previousFrameTime == -1)
  {
    previousFrameTime = gdk_frame_clock_get_frame_time(frame_clock);
    return true; 
  }  

  // g_print("animating...\n");
  // may want to use gdk_frame_clock_get_predicted_presentation_time in the future instead

  int frameTime = gdk_frame_clock_get_frame_time(frame_clock);
  float deltaT = microseconds_to_seconds(frameTime - previousFrameTime);

  playbackTime += deltaT;
  // g_printf("playback time: %f\n", playbackTime);
  if (playbackTime >= (double) pianoGridWidth / tempo)
  {
    // finish playing (will need to adjust conditions later)
    reset_playback(NULL, widget);
    return true;
  }
  
  // update audio
  
  playbackX = (int) (playbackTime * tempo);

  // update scrubber

  int width = gtk_widget_get_allocated_width(widget);
  scrubberPosition = playbackTime * tempo * ((double) width - 2 * pianoRollBorder) / pianoGridWidth;
  previousFrameTime = frameTime;

  // g_print("queueing redraw\n");
  gtk_widget_queue_draw(widget);
    

  return true; // I guess?
}

static void draw_piano_roll(GtkDrawingArea *area,
               cairo_t        *cr,
               int             width,
               int             height,
               gpointer        data)
{

  // https://cairographics.org/manual/cairo-cairo-t.html

  GdkRGBA whiteKeyColor, blackKeyColor, gridLineColor, noteColor, scrubberColor;

  whiteKeyColor.red = 0.8;
  whiteKeyColor.green = 0.8;
  whiteKeyColor.blue = 0.8;
  whiteKeyColor.alpha = 1.0;

  blackKeyColor.red = 0.5;
  blackKeyColor.green = 0.5;
  blackKeyColor.blue = 0.5;
  blackKeyColor.alpha = 1.0;

  gridLineColor.red = 0.0;
  gridLineColor.green = 0.0;
  gridLineColor.blue = 0.0;
  gridLineColor.alpha = 1.0;
  
  noteColor.red = 0.0;
  noteColor.green = 0.0;
  noteColor.blue = 0.0;
  noteColor.alpha = 1.0;

  scrubberColor.red = 1.0;
  scrubberColor.green = 0.0;
  scrubberColor.blue = 0.0;
  scrubberColor.alpha = 1.0;

  // draw key colors
  bool keyColors[] = {1, 0, 1, 0, 1, 1, 0, 1, 0, 1, 0, 1};
  double keyHeight = (double) (height - 2 * pianoRollBorder) / pianoKeyCount;
  double keyWidth = (double) (width - 2 * pianoRollBorder) / pianoGridWidth;

  for (int k = 0; k < pianoKeyCount; k++)
  {
    if (keyColors[k % 12])
    {
      gdk_cairo_set_source_rgba(cr, &whiteKeyColor);
    } 
    else
    {
      gdk_cairo_set_source_rgba(cr, &blackKeyColor);
      
    }
    cairo_rectangle (cr, pianoRollBorder, height - (k + 1) * keyHeight - pianoRollBorder, width - (2 * pianoRollBorder), keyHeight);
    cairo_fill (cr);
  }

  // draw notes
  
  gdk_cairo_set_source_rgba(cr, &noteColor);
  for (int i = 0; i < pianoGridWidth; i++)
  {
    for (int j = 0; j < pianoKeyCount; j++)
    {
      if (get_note(i,j))
      {
        cairo_rectangle(cr, 
                        pianoRollBorder + i * (width - 2 * pianoRollBorder) / pianoGridWidth,
                        height - pianoRollBorder - (j + 1) * keyHeight,
                        (width - 2 * pianoRollBorder) / pianoGridWidth,
                        keyHeight);
        cairo_fill(cr);
      }
    }
  }
  

  // draw grid lilnes

  double gridLineWidth = 2.0;
  cairo_set_line_width(cr, gridLineWidth);  
  gdk_cairo_set_source_rgba(cr, &gridLineColor);

  // horizontal
  for (int k = 1; k < pianoKeyCount; k++)
  {
    cairo_move_to(cr, pianoRollBorder, height - k * keyHeight - pianoRollBorder);
    cairo_line_to(cr, width - pianoRollBorder, height - k * keyHeight - pianoRollBorder);
    cairo_stroke(cr);
  }

  // vertical
  for (int j = 1; j < pianoGridWidth; j++)
  {
    cairo_move_to(cr, j * (width - 2 * pianoRollBorder) / pianoGridWidth + pianoRollBorder, pianoRollBorder);
    cairo_line_to(cr, j * (width - 2 * pianoRollBorder) / pianoGridWidth + pianoRollBorder, height - pianoRollBorder);
    cairo_stroke(cr);
  }

  // draw playback scrubber
  cairo_set_line_width(cr, scrubberWidth);
  gdk_cairo_set_source_rgba(cr, &scrubberColor);
  cairo_move_to(cr, pianoRollBorder + scrubberPosition, pianoRollBorder - scrubberHeightOffset);
  cairo_line_to(cr, pianoRollBorder + scrubberPosition, height - pianoRollBorder + scrubberHeightOffset);
  cairo_stroke(cr);
}

// currently, we simply have a wave for every single possible note :P
ma_waveform** waves;
int selectedWaveform = 0;

static void update_instrument_select(GtkWidget* widget, gpointer data)
{
  int selected = gtk_drop_down_get_selected(GTK_DROP_DOWN(widget));
  if (selected != selectedWaveform)
  {
    g_print("instrument updating...\n");
    selectedWaveform = selected;
    ma_waveform_type type;
    switch(selectedWaveform)
    {
      case(0):
        type = ma_waveform_type_sine;
        break;
      case(1):
        type = ma_waveform_type_square;
        break;
      case(2):
        type = ma_waveform_type_triangle;
        break;
      case(3):
        type = ma_waveform_type_sawtooth;
        break;
    }

    for (int i = 0; i < pianoKeyCount; i++)
    {
      ma_waveform_set_type(waves[i], type);
    }
  }
}

void data_callback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
	if (playing || exporting)
	{
	  // In playback mode copy data to pOutput. In capture mode read data from pInput. In full-duplex mode, both
    // pOutput and pInput will be valid and you can move data from pInput into pOutput. Never process more than
    // frameCount frames.
  
    // MA_ASSERT(pDevice->playback.channels == DEVICE_CHANNELS);

    // MA_ASSERT(pSineWave != NULL);
   
    // g_print("playing or exporting\n"); 
    // g_printf("playbackX = %i\n", playbackX);
 
    float* pOutputF32 = (float*)pOutput;
    for (int i = 0; i < frameCount; i++)
    {
          pOutputF32[i] = 0.0f;
    }

    for (int k = 0; k < pianoKeyCount; k++)
    {
      if (get_note(playbackX, k))
      {
        // read into temporary buffer and then add to output buffer
        float temp[frameCount];
        for (int i = 0; i < frameCount; i++)
        {
          temp[i] = 0.0f;
        }      
        ma_waveform_read_pcm_frames(waves[k], temp, frameCount, NULL);
        // g_print("waveform read pcm frames\n");
        for (int i = 0; i < frameCount; i++)
        {
          pOutputF32[i] += temp[i];
        }
      }
    }
 
		(void)pInput;   /* Unused. */    
	}
  else if (editNoteSoundActive)
  {

    // MA_ASSERT(pDevice->playback.channels == DEVICE_CHANNELS);
	
    // MA_ASSERT(pSineWave != NULL);

    ma_waveform_read_pcm_frames(waves[editY], pOutput, frameCount, NULL);
  }
}



static void export_song(GtkWidget* widget, gpointer data)
{
  
  ma_encoder_config config = ma_encoder_config_init(ma_encoding_format_wav, EXPORT_FORMAT, EXPORT_CHANNELS, EXPORT_SAMPLE_RATE);
  ma_encoder encoder;
  ma_result result = ma_encoder_init_file("my_file.wav", &config, &encoder);
  if (result != MA_SUCCESS) {
    // Error
    g_print("encountered an error while initializing file\n");
    return;
  }
  
  ma_uint64 bufferLength = 1; // IDK if 64 is good here. Can it be a smaller number?

  // iterate through the whole song with the sample rate
  
  double playbackTimeSave = playbackTime;
  int playbackXSave = playbackX;
  playbackTime = 0.0;
  playbackX = 0;
  exporting = true;

  int totalWrittenFrames = 0;
  int totalFramesToWrite = (double)pianoGridWidth / tempo * EXPORT_SAMPLE_RATE;
  
  g_print("Beginning export to file...\n");

  while (totalWrittenFrames < totalFramesToWrite)
  {
    ma_uint64 framesWritten;
    
    float outputBuffer[bufferLength];
    data_callback(&device, &outputBuffer, NULL, bufferLength);    

    result = ma_encoder_write_pcm_frames(&encoder, &outputBuffer, bufferLength, &framesWritten); 
    if (result != MA_SUCCESS) {
      // Error
      g_print("encountered an error while exporting\n");
      
      exporting = false;
      
      return;
    }
    
    /*
    for (int i = 0; i < bufferLength; i++)
    {
      g_printf("Output for frame %i: %f\n", totalWrittenFrames + 1 + i, outputBuffer[i]);  
    }
    */

    totalWrittenFrames += framesWritten;
    playbackTime = (double)totalWrittenFrames / EXPORT_SAMPLE_RATE;
    playbackX = (int) (playbackTime * tempo);
        

    // delete[] outputBuffer;
  }
  
  g_printf("Finished export. Total frames written: %i\n", totalWrittenFrames);

  exporting = false;
  playbackX = playbackXSave;
  playbackTime = playbackTimeSave;
  ma_encoder_uninit(&encoder);
  
}



static void activate (GtkApplication* app, gpointer user_data)
{
	GtkWidget* window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "Window");
	gtk_window_set_default_size(GTK_WINDOW(window), 1000, 1000);  

  GtkWidget* vpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
  // GtkWidget* topFrame = gtk_frame_new (NULL);
  // GtkWidget* bottomFrame = gtk_frame_new (NULL);

  // gtk_paned_set_start_child(GTK_PANED (paned), topFrame);
  

  GtkWidget* menuBox = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
  gtk_paned_set_start_child(GTK_PANED(vpaned), menuBox);
  gtk_widget_set_size_request (menuBox, 75, -1);
  gtk_paned_set_resize_start_child (GTK_PANED(vpaned), FALSE);
  

  GtkWidget* pianoRoll =  gtk_drawing_area_new();
  gtk_drawing_area_set_content_width(GTK_DRAWING_AREA(pianoRoll), 100);
  gtk_drawing_area_set_content_height(GTK_DRAWING_AREA(pianoRoll), 100);
  gtk_drawing_area_set_draw_func(GTK_DRAWING_AREA(pianoRoll), draw_piano_roll, NULL, NULL);

  // piano roll input

  GtkGesture* pianoRollPrimaryDrag = gtk_gesture_drag_new();
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(pianoRollPrimaryDrag), GDK_BUTTON_PRIMARY);
  gtk_widget_add_controller(pianoRoll, GTK_EVENT_CONTROLLER(pianoRollPrimaryDrag));

  g_signal_connect(pianoRollPrimaryDrag, "drag-begin", G_CALLBACK(piano_roll_primary_drag_begin), (void*) pianoRoll);
  g_signal_connect(pianoRollPrimaryDrag, "drag-update", G_CALLBACK(piano_roll_primary_drag_update), (void*) pianoRoll);
  g_signal_connect(pianoRollPrimaryDrag, "drag-end", G_CALLBACK(piano_roll_primary_drag_end), (void*) pianoRoll);

  // GtkShortcut* undoShortcut = gtk_shortcut_new(gtk_shortcut_trigger_parse_string("<Control>Z"), gtk_callback_action_new((GtkShortcutFunc)handle_undo_shortcut, NULL, NULL));
  
  /*
  gtk_widget_class_add_binding (
    GtkWidgetClass* pianoRoll,
    guint (guint)'Z',
    GdkModifierType ,
    GtkShortcutFunc callback,
    const char* format_string
  */


  // piano roll animation
  gtk_widget_add_tick_callback(pianoRoll, animate_piano_roll, NULL, NULL);
  GdkFrameClock* pianoClock = gtk_widget_get_frame_clock(pianoRoll);
  // gdk_frame_clock_end_updating(pianoClock);

  gtk_paned_set_end_child (GTK_PANED(vpaned), pianoRoll);
  gtk_paned_set_resize_end_child (GTK_PANED(vpaned), TRUE);


  gtk_window_set_child(GTK_WINDOW(window), vpaned);


  // buttons:
  
  /*
  GtkTooltips* buttonTooltips;
  buttonTooltips = gtk_tooltips_new();
  gtk_tooltips_set_colors(buttonTooltips, )
  */  

  GtkWidget* exportButton = gtk_button_new_with_label("Export");  
  g_signal_connect (exportButton, "clicked", G_CALLBACK(export_song), NULL);
  gtk_widget_set_tooltip_markup(exportButton, "<span foreground=\"gray\">Exports song to .wav file (WIP)</span>");
  gtk_box_append(GTK_BOX(menuBox), exportButton);

  const char* instrumentStrings[] = {"sine wave", "square wave", "triangle wave", "saw wave"};

  GtkWidget* instrumentSelectButton = gtk_drop_down_new_from_strings(instrumentStrings);  
  gtk_box_append(GTK_BOX(menuBox), instrumentSelectButton);
  // a bit annoying, drop down menus are still in development
  gtk_widget_set_tooltip_markup(instrumentSelectButton, "<span foreground=\"gray\">Instrument selection</span>");
  g_signal_connect(instrumentSelectButton, "state-flags-changed", G_CALLBACK(update_instrument_select), NULL);

  GtkWidget* playButton  = gtk_button_new_with_label("Play");
  g_signal_connect (playButton, "clicked", G_CALLBACK(start_playback), (void*) pianoRoll);
  gtk_widget_set_tooltip_markup(playButton, "<span foreground=\"gray\">Begins audio playback</span>");
  gtk_box_append(GTK_BOX(menuBox), playButton);
  

  GtkWidget* stopButton  = gtk_button_new_with_label("Stop");
  g_signal_connect (stopButton, "clicked", G_CALLBACK(stop_playback), (void*) pianoRoll);
  gtk_widget_set_tooltip_markup(stopButton, "<span foreground=\"gray\">Stops audio playback</span>");
  gtk_box_append(GTK_BOX(menuBox), stopButton);


  GtkWidget* resetButton  = gtk_button_new_with_label("Reset");
  g_signal_connect (resetButton, "clicked", G_CALLBACK(reset_playback), (void*) pianoRoll);
  gtk_widget_set_tooltip_markup(resetButton, "<span foreground=\"gray\">Returns playback scrubber to start of song</span>");
  gtk_box_append(GTK_BOX(menuBox), resetButton);

  GtkWidget* clearButton  = gtk_button_new_with_label("Clear");
  g_signal_connect (clearButton, "clicked", G_CALLBACK(clear_notes), (void*) pianoRoll);
  gtk_widget_set_tooltip_markup(clearButton, "<span foreground=\"gray\">Clears piano roll (Only one clear can be undone!)</span>");
  gtk_box_append(GTK_BOX(menuBox), clearButton);

  GtkWidget* undoButton  = gtk_button_new_with_label("Undo");
  g_signal_connect (undoButton, "clicked", G_CALLBACK(undo), (void*) pianoRoll);
  gtk_widget_set_tooltip_markup(undoButton, "<span foreground=\"gray\">Undoes piano roll action</span>");
  gtk_box_append(GTK_BOX(menuBox), undoButton);

  GtkWidget* redoButton  = gtk_button_new_with_label("Redo");
  g_signal_connect (redoButton, "clicked", G_CALLBACK(redo), (void*) pianoRoll);
  gtk_widget_set_tooltip_markup(redoButton, "<span foreground=\"gray\">Redoes piano roll action</span>");
  gtk_box_append(GTK_BOX(menuBox), redoButton);
  


	// gtk_widget_set_halign(box, GTK_ALIGN_CENTER);
	// gtk_widget_set_valign(box, GTK_ALIGN_CENTER);


	// g_signal_connect(playButton, "pressed", G_CALLBACK(play_sound), NULL);
	// g_signal_connect_swapped(playButton, "clicked", G_CALLBACK(gtk_window_destroy), window);

	
	
	

	gtk_window_present(GTK_WINDOW(window));
}

void delete_waves()
{
  for (int w = 0; w < pianoKeyCount; w++)
  {
    ma_waveform_uninit(waves[w]);
    delete waves[w];
  }
  delete waves;
}

int main(int argc, char** argv)
{
	
	
	ma_device_config config = ma_device_config_init(ma_device_type_playback);
  config.playback.format   = ma_format_f32;   // Set to ma_format_unknown to use the device's native format.
  config.playback.channels = 1;               // Set to 0 to use the device's native channel count.
  config.sampleRate        = 48000;           // Set to 0 to use the device's native sample rate.
  config.dataCallback      = data_callback;   // This function will be called when miniaudio needs more data.
  config.pUserData         = NULL;

  if (ma_device_init(NULL, &config, &device) != MA_SUCCESS) {
      return -1;  // Failed to initialize the device.
  }

  waves = new ma_waveform*[pianoKeyCount];

  for (int w = 0; w < pianoKeyCount; w++)
  {
    ma_waveform* wave = new ma_waveform;
    waves[w] = wave;
    ma_waveform_config sineWaveConfig = ma_waveform_config_init(device.playback.format, device.playback.channels, device.sampleRate, ma_waveform_type_sine, 0.2, pitch_from_note(w + baseKeyNote));
    ma_waveform_init(&sineWaveConfig, wave);
  }  
  
  ma_device_start(&device);     // The device is sleeping by default so you'll need to start it manually.

  

	GtkApplication* app;
	int status;

	app = gtk_application_new("org.gtk.example", G_APPLICATION_FLAGS_NONE);
	g_signal_connect(app, "activate", G_CALLBACK(activate), NULL);


  init_notes();

	// init main loop
	status = g_application_run(G_APPLICATION(app), argc, argv);
	
	// exit
	g_object_unref(app);

  delete_waves();

	ma_device_uninit(&device);

  delete_notes();

	return status;
}
