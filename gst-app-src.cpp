// example appsrc for gstreamer 1.0 with own mainloop & external buffers. based on example from gstreamer docs.
// public domain, 2015 by Florian Echtler <floe@butterbrot.org>. compile with:
// gcc --std=c99 -Wall $(pkg-config --cflags gstreamer-1.0) -o gst gst.c $(pkg-config --libs gstreamer-1.0) -lgstapp-1.0

#include <gst/gst.h>
#include <gst/app/gstappsrc.h>
#include <stdio.h>
#include "FlyCapture2.h"
#include <iostream>
#include <sstream>

#include <stdint.h>
#include <stdlib.h>

#include <time.h>
#include <signal.h>

#define WIDTH 1280
#define HEIGHT 720

#define NUM_FRAMES_TO_CAPTURE 100000
#define TARGET_BITRATE 10000000

class gstPGAppSrc {
public:
	gstPGAppSrc(int w, int h, int numFrames) :
		captureWidth(w), captureHeight(h), numFramesToCapture(numFrames),
		imageSize(0)
	{}
	~gstPGAppSrc(void) {}
	
	int factory_make(char* location);
	int pipeline_make();
	int watcher_make();
	
	bool OpenCamera(void);
	bool grabFrame(FlyCapture2::Image *frame);
	int getFrameSize() { return imageSize; }
	
private:	
	int captureWidth;
	int captureHeight;
	int numFramesToCapture;

	GstElement *source;
	GstElement *filter1; 
	GstElement *convert;
	GstElement *accel;
	GstElement *encoder; 
	GstElement *filter2;
	GstElement *queue; 
	GstElement *parser;
	GstElement *muxer; 
	GstElement *sink;	
	
	GstCaps *filter1_caps;
	GstCaps *filter2_caps;
	
	GstBus *bus;
	
	guint bus_watch_id;
	
	//! Point Grey objects:
	
	//! Point Grey Camera:
	FlyCapture2::Camera pg_camera;
	FlyCapture2::Error pg_error;
	FlyCapture2::EmbeddedImageInfo pg_embeddedInfo;
	FlyCapture2::ImageMetadata pg_metadata;
	FlyCapture2::CameraInfo pg_camInfo;
	FlyCapture2::Format7ImageSettings fmt7ImageSettings;
	FlyCapture2::Format7PacketInfo fmt7PacketInfo;
	FlyCapture2::Format7Info fmt7Info;
	FlyCapture2::PixelFormat k_fmt7PixFmt;
	FlyCapture2::Mode k_fmt7Mode;
	FlyCapture2::Image pg_rawImage;
	int imageSize;

	
};

static GMainLoop *loop;
GstElement *pipeline;

static void sigint_restore (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = SIG_DFL;

  sigaction (SIGINT, &action, NULL);
}

/* Signal handler for ctrl+c */
void intHandler(int dummy) {	
	//! Emit the EOS signal which tells all the elements to shut down properly:
	printf("Sending EOS signal to shutdown pipeline cleanly\n");
	gst_element_send_event(pipeline, gst_event_new_eos());
	sigint_restore();
	return;
}

static gboolean bus_call	(GstBus     *bus,
							 GstMessage *msg,
							 gpointer    data)
{
	GMainLoop *loop = (GMainLoop *) data;

	switch (GST_MESSAGE_TYPE (msg)) {

		case GST_MESSAGE_EOS:
			g_print ("End of stream\n");
			g_main_loop_quit (loop);
		break;

		case GST_MESSAGE_ERROR: {
			gchar  *debug;
			GError *error;

			gst_message_parse_error (msg, &error, &debug);
			g_free (debug);

			g_printerr ("Error: %s\n", error->message);
			g_error_free (error);

			g_main_loop_quit (loop);
		break;
		}
		
		default:
		break;
	}

	return TRUE;
}
//uint8_t buffer[WIDTH*HEIGHT*3];

double ms_time(void)
{
    struct timespec now_timespec;
    clock_gettime(CLOCK_MONOTONIC, &now_timespec);
    return ((double)now_timespec.tv_sec)*1000.0 + ((double)now_timespec.tv_nsec)*1.0e-6;
}

bool gstPGAppSrc::grabFrame(FlyCapture2::Image *frame) 
{
	pg_error = pg_camera.RetrieveBuffer( frame );
    if (pg_error != FlyCapture2::PGRERROR_OK)
    {
        printf("Failed to capture image from PG Camera!\n");
        return false;
    } 	
    return true;
}

static void cb_need_data (GstElement *appsrc, guint unused_size, gpointer user_data) {
  
	gstPGAppSrc *app = (gstPGAppSrc*)user_data;
  	static gboolean white = FALSE;
	static double last_cap_time = ms_time();
	static double frame_rate_avg = 30.0;
	static double frame_dt_ms = 33.0;
	static double last_frameRate_count_time = ms_time() - 1001.0;
	static int last_frameCount = 0;
	static int frameCount = 0;
	static bool once = false;
	GstBuffer *buffer;
	guint size;
	GstFlowReturn ret;
	
	double cap_time = ms_time();
	
	FlyCapture2::Image image;
	if (!app->grabFrame(&image)) {
		printf("Failed to grab!\n");
		return;
	}
	int imageSize = app->getFrameSize();
    
    //if (ret != GST_FLOW_OK) {
		/* something wrong, stop pushing */		
		//printf("Something went wrong.  Stop pushing!\n");
		//! Emit the EOS signal which tells all the elements to shut down properly:
		//gst_element_send_event(pipeline, gst_event_new_eos());
	//}
    
    buffer = gst_buffer_new_wrapped_full( 
				(GstMemoryFlags)0,
				(gpointer)image.GetData(),
				imageSize,
				0,
				imageSize,
				NULL,
				NULL);
	
	g_signal_emit_by_name (appsrc, "push-buffer", buffer, &ret);
	
	if (!once) {
		once = true;
	} else {
		frame_dt_ms = cap_time - last_cap_time;
		if (cap_time - last_frameRate_count_time > 1000.0) {
			int framesInOneSecond = frameCount - last_frameCount;
			printf("Frame Rate: %d\n", framesInOneSecond);
			last_frameRate_count_time = cap_time;
			last_frameCount = frameCount;			
		}
	}
	
	last_cap_time = cap_time;
	frameCount++;  
}

bool gstPGAppSrc::OpenCamera(void) {
	//! Create a camera and connect to it (this is FlyCapture code):

    //! Connect Point Grey Camera:
    pg_error = pg_camera.Connect ( 0 );
    if (pg_error != FlyCapture2::PGRERROR_OK)
    {
        printf("Failed to connect to a PG Camera.  Check connection!\n");
        return false;
    }

    printf("  $$ Opened Point Grey camera!\n");

    pg_error = pg_camera.GetEmbeddedImageInfo( &pg_embeddedInfo );
    if (pg_error != FlyCapture2::PGRERROR_OK)
    {
        printf("Unable to get flycapture camera info:\n");
        pg_error.PrintErrorTrace();
        return false;
    }

    //! Turn on time stamp on PG camera if it's off:
    if ( pg_embeddedInfo.timestamp.onOff == false )
    {
        printf("PG Camera: Timestamps were off.  Turning them on!\n");
        pg_embeddedInfo.timestamp.onOff = true;
    }

    if ( pg_embeddedInfo.frameCounter.onOff == false )
    {
        printf("PG Camera: Enabling frame counter\n");
        pg_embeddedInfo.frameCounter.onOff = true;
    }

    pg_error = pg_camera.SetEmbeddedImageInfo( &pg_embeddedInfo );
    if (pg_error != FlyCapture2::PGRERROR_OK )
    {
        printf("Unable to turn on timestamp feature on PG Camera\n");
        pg_error.PrintErrorTrace();
    }

    //! Get the camera info and print it out:
    pg_error = pg_camera.GetCameraInfo( &pg_camInfo );
    if (pg_error != FlyCapture2::PGRERROR_OK )
    {
        printf("Unable to get PG Camera info\n");
        pg_error.PrintErrorTrace();
    }

    printf("Fly Capture Camera Information:\n");
    printf("  Vendor: %s\n", pg_camInfo.vendorName);
    printf("  Model: %s\n", pg_camInfo.modelName);
    printf("  Serial Num: %d\n", pg_camInfo.serialNumber);

    /// Need to find a place for these variables
    k_fmt7Mode = FlyCapture2::MODE_0;
    
    k_fmt7PixFmt = FlyCapture2::PIXEL_FORMAT_RGB;

    //! Check if mode is supported
    bool supported;
    fmt7Info.mode = k_fmt7Mode;
    pg_error = pg_camera.GetFormat7Info( &fmt7Info, &supported );
    if (pg_error != FlyCapture2::PGRERROR_OK )
    {
        printf("Format Mode not supported\n");
        pg_error.PrintErrorTrace();
    }
    //! Check if pixel format is supported
     if ( (k_fmt7PixFmt & fmt7Info.pixelFormatBitField) == 0 )
    {
		printf("Pixel Format not supported\n");
        pg_error.PrintErrorTrace();
    }
    //! Create struct with new settings
    fmt7ImageSettings.mode = k_fmt7Mode;
    fmt7ImageSettings.offsetX = (fmt7Info.maxWidth-WIDTH)/2;
    fmt7ImageSettings.offsetY = (fmt7Info.maxHeight-HEIGHT)/2;
    fmt7ImageSettings.width = WIDTH;
    fmt7ImageSettings.height = HEIGHT;
    fmt7ImageSettings.pixelFormat = k_fmt7PixFmt;

    //! Validate new settings
    bool valid;
    // Validate the settings to make sure that they are valid
    pg_error = pg_camera.ValidateFormat7Settings(&fmt7ImageSettings,&valid, &fmt7PacketInfo );
    if (pg_error != FlyCapture2::PGRERROR_OK )
    {
        printf("Cannot validate format settings\n");
        pg_error.PrintErrorTrace();
    }

    if ( !valid )
    {
		printf("Format settings required are not supported\n");
        pg_error.PrintErrorTrace();
        return false;
    }

    //! Set the new settings to the camera
    pg_error = pg_camera.SetFormat7Configuration(&fmt7ImageSettings,fmt7PacketInfo.recommendedBytesPerPacket );
    if (pg_error != FlyCapture2::PGRERROR_OK )
    {
        printf("Unable to set 7 camera settings\n");
        pg_error.PrintErrorTrace();
    }
   
    //! Start capturing images
    pg_error = pg_camera.StartCapture();
    if (pg_error == FlyCapture2::PGRERROR_ISOCH_BANDWIDTH_EXCEEDED)
    {
        printf("Bandwidth exceeded!\n");
        return false;
    }
    else if (pg_error != FlyCapture2::PGRERROR_OK)
    {
        printf("Error starting capture!\n");
        pg_error.PrintErrorTrace();
    }
    
    //! Capture first image to make sure things are working ok:
    pg_error = pg_camera.RetrieveBuffer( &pg_rawImage );
    if (pg_error != FlyCapture2::PGRERROR_OK)
    {
        printf("Failed to capture first image from PG Camera!\n");
        return false;
    }
    
    //! Create dump file if we are saving images:
    imageSize = pg_rawImage.GetDataSize();
    
    printf("  IMAGE SIZE: %d\n", imageSize);
	return true;
}

int gstPGAppSrc::factory_make(char* location)
{
	/* Create gstreamer elements */
	pipeline = gst_pipeline_new ("pipeline");
	source = gst_element_factory_make ("appsrc", "source");
	//source   = gst_element_factory_make ("videotestsrc",	"videotestsrc");
	filter1  = gst_element_factory_make ("capsfilter",	"filter1");
	convert	 = gst_element_factory_make ("videoconvert", "conv");
	encoder  = gst_element_factory_make ("omxh264enc",	"dmaienc_h264");
	filter2	 = gst_element_factory_make ("capsfilter", "filter2");
	parser	 = gst_element_factory_make ("h264parse", "parser");
	//! Note: qtmux is good if you can cleanly exit the app and send EOS upstream.  Otherwise, your files are corrupted.  
	muxer    = gst_element_factory_make ("qtmux",			"mux");
	//GstElement *muxer	 = gst_element_factory_make ("mpegtsmux", "mpegmux");
	sink     = gst_element_factory_make ("filesink",		"filesink");
	//GstElement *fakesink = gst_element_factory_make ("fakesink", "fakey");

	//! setup
	g_object_set (G_OBJECT (source), "caps",
		gst_caps_new_simple ("video/x-raw",
					 "format", G_TYPE_STRING, "RGB",
					 "width", G_TYPE_INT, WIDTH,
					 "height", G_TYPE_INT, HEIGHT,
					 "framerate", GST_TYPE_FRACTION, 0, 1,
					 NULL), NULL);
					 
	/* Video caps */
	filter1_caps = gst_caps_new_simple ("video/x-raw",
		"format", G_TYPE_STRING, "RGB",
		"width", G_TYPE_INT, WIDTH,
		"height", G_TYPE_INT, HEIGHT,
		NULL);
	
	filter2_caps = gst_caps_new_simple ("video/x-h264",
		"stream-format", G_TYPE_STRING, "byte-stream",
		NULL);

	if (!pipeline) {
		g_printerr ("Pipeline could not be created. Exiting.\n");
		return -1;
	}
	if (!source) {
		g_printerr ("Source could not be created. Exiting.\n");
		return -1;
	}
	if (!filter1) {
		g_printerr ("Filter 1 could not be created. Exiting.\n");
		return -1;
	}
	if (!convert) {
		g_printerr ("Convert element could not be created. Exiting.\n");
		return -1;
	}
	if (!encoder) {
		g_printerr ("Encoder element could not be created. Exiting.\n");
		return -1;
	}
	if (!filter2) {
		g_printerr ("Filter 2 element could not be created. Exiting.\n");
		return -1;
	}
	if (!parser) {
		g_printerr ("Parser element could not be created. Exiting.\n");
		return -1;
	}
	if (!muxer) {
		g_printerr ("Muxer element could not be created. Exiting.\n");
		return -1;
	}
	if (!sink) {
		g_printerr ("Sink element could not be created. Exiting.\n");
		return -1;
	}
	if (!filter1_caps) {
		g_printerr ("Filter1 caps could not be created. Exiting.\n");
		return -1;
	}
	if (!filter2_caps) {
		g_printerr ("Filter 2 caps could not be created. Exiting.\n");
		return -1;
	}

	/* Set up elements */
	g_object_set (G_OBJECT (filter1), "caps", filter1_caps, NULL);
	g_object_set (G_OBJECT (filter2), "caps", filter2_caps, NULL);
	gst_caps_unref (filter1_caps);
	gst_caps_unref (filter2_caps);	
	g_object_set (G_OBJECT (source), "num-buffers", NUM_FRAMES_TO_CAPTURE, NULL);
	
	g_object_set (G_OBJECT (encoder), "bitrate", TARGET_BITRATE, NULL);

	/* we set the input filename to the source element */
	g_object_set (G_OBJECT (sink), "location", location, NULL);	
	
	/* setup appsrc */
	g_object_set (G_OBJECT (source),
		"stream-type", GST_APP_STREAM_TYPE_STREAM,
		"format", GST_FORMAT_TIME,
		"is-live", TRUE,
		"do-timestamp", TRUE,
		NULL);
	g_signal_connect (source, "need-data", G_CALLBACK (cb_need_data), this);
	
	return 0;	
}

int gstPGAppSrc::pipeline_make()
{
	/* we add all elements into the pipeline */
	gst_bin_add_many (GST_BIN (pipeline),
		source, filter1, convert, encoder, filter2, parser, muxer, sink, NULL);
	/* we link the elements together */
	gst_element_link_many (source, filter1, convert, encoder, filter2, parser, muxer, sink, NULL);	

	return 0;
}

int gstPGAppSrc::watcher_make()
{
	/* we add a message handler */
	bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
	bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
	gst_object_unref (bus);	
	return 0;
}

gint main (gint argc, gchar *argv[]) {

	//GstElement *pipeline, , *queue, *videosink;
	signal(SIGINT, intHandler);

	gstPGAppSrc app(WIDTH, HEIGHT, NUM_FRAMES_TO_CAPTURE);

	if (!app.OpenCamera()) {
	  return -1;
	}  

	/* init GStreamer */
	gst_init (&argc, &argv);

	loop = g_main_loop_new (NULL, FALSE);

	//! Try one more similar to RidgeRun's method:
	//! This pipeline works well.  Now, to hook it up to our appsrc:
//gst-launch-1.0 videotestsrc \
	! 'video/x-raw, format=(string)RGB, width=(int)800, height=(int)600' \
	! videoconvert \
	! omxh264enc \
	! 'video/x-h264, stream-format=(string)byte-stream' \
	! h264parse \
	! qtmux \
	! filesink location=testconvert2.mp4 -e
	
	
	/* Check input arguments */
	if (argc != 2) {
		g_printerr ("Usage: %s <Recorded file name eg: test.h264>\n", argv[0]);
		return -1;
	}
	
	/* Initialize elements */
	if(app.factory_make(argv[1]) != 0)
		return -1;
		
	/* Add function to watch bus */
	if(app.watcher_make() != 0)
		return -1;

	/* Add elements to pipeline, and link them */
	if(app.pipeline_make() != 0)
		return -1;

	/* play */
	gst_element_set_state (pipeline, GST_STATE_PLAYING);

	g_main_loop_run(loop);

	/* clean up */
	gst_element_set_state (pipeline, GST_STATE_NULL);
	gst_object_unref (GST_OBJECT (pipeline));
	g_main_loop_unref (loop);

	return 0;
}
