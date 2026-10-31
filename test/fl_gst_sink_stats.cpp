#include <gst/gst.h>
#include <cstdio>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"failed %d: %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char**argv){
 gst_init(&argc,&argv);CHECK(argc==2);GError* error=nullptr;
 auto* plugin=gst_plugin_load_file(argv[1],&error);CHECK(plugin);gst_object_unref(plugin);
 auto* pipeline=gst_parse_launch("videotestsrc num-buffers=3 ! video/x-raw,format=BGRA,width=16,height=16 ! framelinksink sync=false channel=stats-no-consumer",&error);CHECK(pipeline && !error);
 auto* bus=gst_element_get_bus(pipeline);CHECK(gst_element_set_state(pipeline,GST_STATE_PLAYING)!=GST_STATE_CHANGE_FAILURE);
 bool stats=false,eos=false;
 while(!eos){auto* m=gst_bus_timed_pop_filtered(bus,5*GST_SECOND,GstMessageType(GST_MESSAGE_ELEMENT|GST_MESSAGE_EOS|GST_MESSAGE_ERROR));CHECK(m);CHECK(GST_MESSAGE_TYPE(m)!=GST_MESSAGE_ERROR);
  if(GST_MESSAGE_TYPE(m)==GST_MESSAGE_ELEMENT){auto* s=gst_message_get_structure(m);if(gst_structure_has_name(s,"framelink-sink-stats")){guint64 received=0,submitted=0,dropped=0,full=0;CHECK(gst_structure_get_uint64(s,"received",&received));CHECK(gst_structure_get_uint64(s,"submitted",&submitted));CHECK(gst_structure_get_uint64(s,"dropped",&dropped));CHECK(gst_structure_get_uint64(s,"ring-full-drops",&full));CHECK(received==3 && submitted==0 && dropped==3 && full==0);stats=true;}}
  eos=GST_MESSAGE_TYPE(m)==GST_MESSAGE_EOS;gst_message_unref(m);
 }
 CHECK(stats);gst_element_set_state(pipeline,GST_STATE_NULL);gst_object_unref(bus);gst_object_unref(pipeline);puts("EOS reports transport loss instead of counting inputs as delivered");return 0;
}
