#include <gst/gst.h>
#include <stdio.h>
#include <linux/input.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
 
static gboolean
bus_call (GstBus * bus, GstMessage * msg, gpointer data)
{
    GMainLoop *loop = (GMainLoop *) data;
    switch (GST_MESSAGE_TYPE (msg)) {
        case GST_MESSAGE_EOS:
            g_print ("End of stream\n");
            g_main_loop_quit (loop);
            break;
        case GST_MESSAGE_ERROR:{
            gchar *debug;
            GError *error;
            gst_message_parse_error (msg, &error, &debug);
            g_printerr ("ERROR from element %s: %s\n",
                        GST_OBJECT_NAME (msg->src), error->message);
            if (debug)
                g_printerr ("Error details: %s\n", debug);
            g_free (debug);
            g_error_free (error);
            g_main_loop_quit (loop);
            break;
        }
        default:
            break;
    }
    return TRUE;
}
 
static void
parse_input (gchar * input)
{
  fflush (stdout);
  memset (input, '\0', 128 * sizeof (*input));
 
  if (!fgets (input, 128, stdin) ) {
    g_print ("Failed to parse input!\n");
    return;
  }
 
  // Clear trailing whitespace and newline.
  g_strchomp (input);
}
 
int main(int argc, char *argv[]) {
  GstElement *pipeline, *v4l2_src, *video_convert, *capsfilter, *tee, *disp_queue, *xvimage_sink;
  GstElement *record0_queue, *x264enc0, *mp4mux0, *file_sink0, *record_queue,*x264enc,*mp4mux,*file_sink;
  GstPad *tee_disp_pad, *tee_record_pad, *tee_record0_pad;
  GstPad *queue_record0_pad, *queue_record_pad, *queue_disp_pad;
  GstCaps *filtercaps;
 
  // Initialize GStreamer
  gst_init (&argc, &argv);
 
  // Create the elements
  v4l2_src = gst_element_factory_make ("v4l2src", "v4l2_src");
  video_convert = gst_element_factory_make ("videoconvert", "video_convert");
  capsfilter = gst_element_factory_make ("capsfilter", "capsfilter");
 
  tee = gst_element_factory_make ("tee", "tee");
  
  disp_queue = gst_element_factory_make ("queue", "disp_queue");
  xvimage_sink = gst_element_factory_make ("xvimagesink", "xvimage_sink");
  
  record0_queue = gst_element_factory_make ("queue", "record0_queue");
  x264enc0 = gst_element_factory_make ("x264enc", "x264enc0");
  mp4mux0 = gst_element_factory_make ("mp4mux", "mp4mux0");
  file_sink0 = gst_element_factory_make ("filesink", "file_sink0");  
 
  //Create the empty pipeline
  pipeline = gst_pipeline_new ("test-pipeline");
 
  if (!pipeline || !v4l2_src || !video_convert || !capsfilter || !tee || !disp_queue || !xvimage_sink ) {
    g_printerr ("Not all elements could be created.\n");
    return -1;
  }
 
  //Configure elements
  // Link all elements that can be automatically linked because they have "Always" pads
  gst_bin_add_many (GST_BIN (pipeline), v4l2_src, video_convert, capsfilter, tee, disp_queue, xvimage_sink, record0_queue, x264enc0, mp4mux0, file_sink0, NULL);
  if (gst_element_link_many (v4l2_src, video_convert, capsfilter, tee, NULL) != TRUE ||
      gst_element_link_many (disp_queue, xvimage_sink, NULL) != TRUE || 
      gst_element_link_many (record0_queue, x264enc0, mp4mux0, file_sink0, NULL) != TRUE) {
    g_printerr ("Elements could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }
 
  // Manually link the Tee, which has "Request" pads
  tee_disp_pad = gst_element_request_pad_simple (tee, "src_%u");
  g_print ("Obtained request pad %s for disp branch.\n", gst_pad_get_name (tee_disp_pad));
  queue_disp_pad = gst_element_get_static_pad (disp_queue, "sink");
  
  if (gst_pad_link (tee_disp_pad, queue_disp_pad) != GST_PAD_LINK_OK ) {
    g_printerr ("Tee could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }
  
    // Manually link the Tee, which has "Request" pads
  tee_record0_pad = gst_element_request_pad_simple (tee, "src_%u");
  g_print ("Obtained request pad %s for disp branch.\n", gst_pad_get_name (tee_record0_pad));
  queue_record0_pad = gst_element_get_static_pad (record0_queue, "sink");
  
  if (gst_pad_link (tee_record0_pad, queue_record0_pad) != GST_PAD_LINK_OK ) {
    g_printerr ("Tee could not be linked.\n");
    gst_object_unref (pipeline);
    return -1;
  }
  g_object_set (G_OBJECT(file_sink0),"location","video1.mp4",NULL);
  g_object_set (G_OBJECT(file_sink0),"async",FALSE,"sync",FALSE, NULL);
  
  filtercaps = gst_caps_new_simple ("video/x-raw",
               "format", G_TYPE_STRING, "NV12",
               "width", G_TYPE_INT, 640,
               "height", G_TYPE_INT, 480,
               "framerate", GST_TYPE_FRACTION, 30, 1,
               NULL);
  g_object_set (G_OBJECT (capsfilter), "caps", filtercaps, NULL);
  gst_caps_unref (filtercaps);
  
  g_object_set (G_OBJECT(xvimage_sink),"async",FALSE,"sync",FALSE, NULL);
 
  g_object_set (G_OBJECT(disp_queue),"max-size-buffers",0,NULL);
  g_object_set (G_OBJECT(disp_queue),"max-size-time",0,NULL);
  g_object_set (G_OBJECT(disp_queue),"max-size-bytes",512000000,NULL);
 
  gst_object_unref (queue_disp_pad);
 
 
  // Start playing the pipeline
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
 
 
 GstBus *bus = NULL;
 GMainLoop *loop = NULL;
 guint bus_watch_id;
 loop = g_main_loop_new (NULL, FALSE);
 bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
 bus_watch_id = gst_bus_add_watch (bus, bus_call, loop);
 gst_object_unref (bus);
 
 {
   g_print("test start \n"); 
   gchar *input = g_new0 (gchar, 128);
   gboolean active = 1;
    while( active ) {
      g_print("input option videos videoq q : \n");
      parse_input(input);
      if(g_str_equal (input, "videos"))
      {
          g_print("video start !\n");
          gst_element_set_state (pipeline, GST_STATE_PAUSED);
          gst_element_set_state (pipeline, GST_STATE_PLAYING);
          record_queue = gst_element_factory_make ("queue", "record_queue");
          /*video_rate = gst_element_factory_make ("videorate", "video_rate");
          jpeg_enc = gst_element_factory_make ("jpegenc", "jpeg_enc");
          avi_mux = gst_element_factory_make ("avimux", "avi_mux");*/
          x264enc = gst_element_factory_make ("x264enc", "x264enc");
          mp4mux = gst_element_factory_make ("mp4mux", "mp4mux");
          file_sink = gst_element_factory_make ("filesink", "file_sink"); 
          
          if ( !record_queue || !x264enc || !mp4mux  || !file_sink ) {
            g_printerr ("Not all elements could be created.\n");
            return -1;
          }
          
            gst_bin_add_many (GST_BIN (pipeline), record_queue, x264enc, mp4mux, file_sink, NULL);
            
            if ( gst_element_link_many (record_queue, x264enc, mp4mux, file_sink, NULL) != TRUE) {
                  g_printerr ("Elements could not be linked.\n");
                  gst_object_unref (pipeline);
                  return -1;
             }
             g_object_set (G_OBJECT(file_sink),"location","video21.mp4",NULL);
             g_object_set (G_OBJECT(file_sink),"async",FALSE,"sync",FALSE, NULL);
             
             
             gst_element_sync_state_with_parent(record_queue);
             /*
             gst_element_sync_state_with_parent(video_rate);
             gst_element_sync_state_with_parent(jpeg_enc);
             gst_element_sync_state_with_parent(avi_mux);
             gst_element_sync_state_with_parent(file_sink);*/
             
             tee_record_pad = gst_element_request_pad_simple (tee, "src_%u");
             g_print ("Obtained request pad %s for record branch.\n", gst_pad_get_name (tee_record_pad));
             queue_record_pad = gst_element_get_static_pad (record_queue, "sink");
             
             g_print("video link start !\n");
             if ( gst_pad_link (tee_record_pad, queue_record_pad) != GST_PAD_LINK_OK) {
                 g_printerr ("Tee could not be linked.\n");
                 gst_object_unref (pipeline);
                 return -1;
             }
             
             
 		GstIterator *iterator;
 		gboolean done = FALSE;
		GValue value = { 0, };
 	 	GstPad *pad;
 		//BufferCountData *bcd;
             
             iterator = gst_element_iterate_pads (tee);
  	     while (!done) {
    	     	switch (gst_iterator_next (iterator, &value)) {
      	     	case GST_ITERATOR_OK:
      	     	 	pad = g_value_dup_object (&value);
			g_print ("Obtained request iterator pad %s for record branch.\n", gst_pad_get_name (pad));
                 	g_value_reset (&value);
        		 break;
      		//case GST_ITERATOR_RESYNC:
        	//	gst_iterator_resync (iterator);
      		//  	break;
      		case GST_ITERATOR_ERROR:
      		  	done = TRUE;
      		  	break;
      		case GST_ITERATOR_DONE:
      		  	done = TRUE;
      		        break;
    		}
             }
             gst_element_set_state (record_queue, GST_STATE_PLAYING);
             gst_element_set_state (x264enc, GST_STATE_PLAYING);
             gst_element_set_state (mp4mux, GST_STATE_PLAYING);
             gst_element_set_state (file_sink, GST_STATE_PLAYING);
             
             gst_element_set_state (pipeline, GST_STATE_PLAYING);
               
      } else if (g_str_equal (input, "videoq"))
      {
         g_print("video quit !\n");
         
         gst_element_send_event(x264enc, gst_event_new_eos());
         
         gst_element_set_state (record_queue, GST_STATE_NULL);
         gst_element_set_state (x264enc, GST_STATE_NULL);
         gst_element_set_state (mp4mux, GST_STATE_NULL);
         gst_element_set_state (file_sink, GST_STATE_NULL);
         
         g_print("video unlink !\n");
         //appctx->caps_video_pad = gst_element_get_static_pad (appctx->v_capsfilter, "sink");
        if( gst_pad_unlink(tee_record_pad, queue_record_pad) != TRUE )
        {
          g_printerr ("video could not be unlinked.\n");
          return -1;
        }
        
        gst_element_release_request_pad (tee, tee_record_pad);

        gst_object_unref (tee_record_pad);
        gst_object_unref (queue_record_pad);
        
        gst_bin_remove(GST_BIN (pipeline), record_queue);
        gst_bin_remove(GST_BIN (pipeline), x264enc);
        gst_bin_remove(GST_BIN (pipeline), mp4mux);
        gst_bin_remove(GST_BIN (pipeline), file_sink);
      
      } else {
       gst_element_send_event(x264enc0, gst_event_new_eos());
       g_print("quit\n");
       active = 0;
     }  
   }
 
 }
 
  g_main_loop_run (loop);
 
  gst_element_release_request_pad (tee, tee_disp_pad);
  gst_object_unref (tee_disp_pad);
 
  gst_element_set_state (pipeline, GST_STATE_NULL);

  gst_object_unref (pipeline);
  gst_deinit();
  return 0;
}
