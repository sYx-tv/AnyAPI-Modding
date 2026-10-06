// Native primitive settings rows; registration, persistence and mod policy are external.
using ToggleFn=void(*)(uint8_t*,void*,const GeoString16*,const bool*,const uintptr_t*,const bool*);
using FloatFn=void(*)(uint8_t*,void*,const GeoString16*,const double*,const double*,const double*,double*,const bool*);
using IntFn=void(*)(uint8_t*,void*,const GeoString16*,const int32_t*,const int32_t*,int32_t*,const bool*);
using TextFn=void(*)(uint8_t*,void*,const GeoString16*,GeoString16*,const GeoString16*,const bool*,const GeoString16*,const bool*,const int32_t*);
using UiFn=void(*)(void*);
using StringCtorFn=void(*)(GeoString16*,const char*);
using StringDtorFn=void(*)(GeoString16*);
static ToggleFn native_toggle{};static FloatFn native_float{};static IntFn native_int{};
static TextFn native_text{};static UiFn native_capture{},native_release{};static EnterFn native_back{};
static StringCtorFn native_string_ctor{};static StringDtorFn native_string_dtor{};
static void release_editing(){auto ui=frontend.load();if(ready&&native_release&&ui)native_release((void*)ui);}
static bool row_begin(const char* id,const char* text){if(!widget(id)||!text||strlen(text)>512)return false;
 auto name=widget_id(id);auto a=make_const_geostring(name.c_str());int32_t columns=2,spacing=0;Vec2S32 size{column_width,compact_height};native_table(native_ui,&a,&columns,&size,&spacing);
 auto label_name=name+".label";auto label_id=make_const_geostring(label_name.c_str()),label_text=make_const_geostring(text);bool background=false;native_label(native_ui,&label_id,&label_text,&background);return true;}
static void edit_focus(uint8_t hovered){if(!hovered)return;uint8_t enter{},back{};native_enter(&enter,native_ui);if(enter)native_capture(native_ui);native_back(&back,native_ui);if(back)native_release(native_ui);}
static uint32_t toggle_row(const char* id,const char* text,uint32_t* value){if(!value||*value>1||!row_begin(id,text))return 0;auto name=widget_id(id)+".value";auto a=make_const_geostring(name.c_str());bool current=*value!=0,focus=false;uintptr_t icon{};safe_read_memory((uintptr_t)native_ui+0x460,&icon,8);uint8_t hover{},enter{};native_toggle(&hover,native_ui,&a,&current,&icon,&focus);if(hover){native_enter(&enter,native_ui);if(enter)*value=!current;}native_end(native_ui);return enter!=0;}
static uint32_t number_row(const char* id,const char* text,double* value,double low,double high,double step,uint32_t integer){if(!value||!std::isfinite(*value)||!std::isfinite(low)||!std::isfinite(high)||!std::isfinite(step)||low>=high||step<=0||step>high-low||*value<low||*value>high||high-low>1e12)return 0;
 if(integer&&(low!=std::floor(low)||high!=std::floor(high)||step!=std::floor(step)||step<1||low<-1e9||high>1e9))return 0;
 if(!row_begin(id,text))return 0;auto name=widget_id(id)+".value";auto a=make_const_geostring(name.c_str());double before=*value;bool focus=false;uint8_t hover{};
 if(integer){int32_t minimum=int32_t(low),maximum=int32_t(high),number=int32_t(std::round(*value));native_int(&hover,native_ui,&a,&minimum,&maximum,&number,&focus);*value=number;}
 else native_float(&hover,native_ui,&a,&low,&high,&step,value,&focus);
 if(!std::isfinite(*value))*value=before;else *value=std::clamp(low+std::round((*value-low)/step)*step,low,high);edit_focus(hover);native_end(native_ui);return before!=*value;}
static uint32_t choice_row(const char* id,const char* text,const char* const* choices,uint32_t count,uint32_t* index){if(!index||!choices||count<1||count>64||*index>=count)return 0;for(uint32_t i=0;i<count;++i)if(!choices[i]||strlen(choices[i])>127)return 0;if(!row_begin(id,text))return 0;
 auto base=std::string(id);layout((base+".field").c_str(),{column_width,compact_height},0,0,0,0);auto before=*index;
 if(sized_button((base+".previous").c_str(),"<",{1,compact_height},0,*index==0))--*index;
 if(sized_button((base+".value").c_str(),choices[*index],{column_width-2,compact_height},0,count==1))*index=(*index+1)%count;
 if(sized_button((base+".next").c_str(),">",{1,compact_height},0,*index+1==count))++*index;
 native_end(native_ui);native_end(native_ui);return before!=*index;}
static uint32_t text_row(const char* id,const char* text,char* utf8,uint32_t capacity){if(!utf8||capacity<2||capacity>257||!memchr(utf8,0,capacity)||!MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,utf8,-1,nullptr,0)||!row_begin(id,text))return 0;
 std::string before=utf8;GeoString16 value{};native_string_ctor(&value,utf8);auto name=widget_id(id)+".value";auto a=make_const_geostring(name.c_str()),empty=make_const_geostring("");bool focus=false,clear=false;int32_t limit=int32_t(capacity)-1;uint8_t results[8]{};
 // This is a game-owned temporary string: construct/destroy using the verified owner cells.
 native_text(results,native_ui,&a,&value,&empty,&focus,&empty,&clear,&limit);
 if(value.length<capacity&&(!value.length||value.data)){std::string after;if(value.length)after.assign((const char*)value.data,value.length);if(after.find('\0')==std::string::npos&&MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,after.c_str(),-1,nullptr,0))memcpy(utf8,after.c_str(),after.size()+1);}
 native_string_dtor(&value);edit_focus(results[1]);native_end(native_ui);return before!=utf8;}
static const AnyMenuV3 api_v3{sizeof(AnyMenuV3),3,&api_v2,toggle_row,number_row,choice_row,text_row};
