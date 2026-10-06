static volatile LONG64 g_p273_definition_attempts[2]{},g_p273_definition_ok[2]{},g_p273_definition_missing[2]{},g_p273_metadata_filled[2]{},g_p273_metadata_match[2]{},g_p273_metadata_mismatch[2]{};
enum P28Reason:uint32_t{P28_NAMED,P28_EMPTY,P28_COMPONENT_NULL,P28_POINTER_READ,P28_DEFINITION_NULL,P28_HEADER_READ,P28_HEADER_INVALID,P28_DATA_NULL,P28_DATA_READ,P28_TEXT_INVALID,P28_NAME_CONFLICT,P28_REASON_COUNT};
static const char* P28_REASON_NAMES[]={"NAMED_CLASS","EMPTY_CLASS","COMPONENT_NULL","COMPONENT_POINTER_UNREADABLE","DEFINITION_NULL","STRING_HEADER_UNREADABLE","STRING_HEADER_INVALID","STRING_DATA_NULL","STRING_DATA_UNREADABLE","STRING_TEXT_INVALID","CLASS_CONFLICT"};
static volatile LONG64 g_p28_reasons[2][P28_REASON_COUNT]{},g_p28_sample_count=0,g_p28_sample_ready[64]{};
struct P28Context{uintptr_t component{},definition{},previous_definition{},previous_vehicle{};uint32_t source{},previous_side{},previous_live{};};
struct P28Sample{uint32_t side{},reason{};P28Context context{};uintptr_t string_data{};uint32_t length{},capacity{};char existing[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{},native_name[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX]{};};
static P28Sample g_p28_samples[64]{};
static size_t p273_side_index(AnymakerVehicleComponentSide side){return side==ANY_COMPONENT_SIDE_SERVER?1:0;}
static void p28_sample(AnymakerVehicleComponentSide side,P28Reason reason,const P28Context& context,const GeoString16& text,const char* existing=nullptr,const char* native_name=nullptr){
    size_t i=p273_side_index(side);LONG64 count=InterlockedIncrement64(&g_p28_reasons[i][reason]);
    if(reason==P28_NAMED || count>(reason==P28_EMPTY?4:8))return;
    LONG64 slot=InterlockedIncrement64(&g_p28_sample_count)-1;if(slot>=64)return;
    auto& sample=g_p28_samples[slot];sample.side=(uint32_t)side;sample.reason=reason;sample.context=context;sample.string_data=text.data;sample.length=text.length;sample.capacity=text.capacity;
    if(existing)std::strncpy(sample.existing,existing,sizeof(sample.existing)-1);
    if(native_name)std::strncpy(sample.native_name,native_name,sizeof(sample.native_name)-1);
    // Publish after copying: worker never touches live native object memory or a partial sample.
    InterlockedExchange64(&g_p28_sample_ready[slot],1);
}
static bool p273_read_component_class(uintptr_t component,AnymakerVehicleComponentSide side,char out[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX],uintptr_t* definition_out=nullptr){
    out[0]=0;if(definition_out)*definition_out=0;size_t i=p273_side_index(side);InterlockedIncrement64(&g_p273_definition_attempts[i]);
    uintptr_t definition=0;GeoString16 name{};P28Context context{};context.component=component;
    auto fail=[&](P28Reason reason){out[0]=0;context.definition=definition;InterlockedIncrement64(&g_p273_definition_missing[i]);p28_sample(side,reason,context,name);return false;};
    if(!component)return fail(P28_COMPONENT_NULL);
    if(!safe_read_memory(component+(side==ANY_COMPONENT_SIDE_SERVER?0x138:0xB0),&definition,8))return fail(P28_POINTER_READ);
    if(!definition)return fail(P28_DEFINITION_NULL);
    if(!safe_read_memory(definition+0x30,&name,sizeof(name)))return fail(P28_HEADER_READ);
    if(name.length>name.capacity || name.capacity>1024u*1024u || name.length>=ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX)return fail(P28_HEADER_INVALID);
    context.definition=definition;if(definition_out)*definition_out=definition;
    if(!name.length){p28_sample(side,P28_EMPTY,context,name);return true;}
    if(!name.data)return fail(P28_DATA_NULL);
    if(!safe_read_memory(name.data,out,name.length))return fail(P28_DATA_READ);
    for(uint32_t n=0;n<name.length;++n)if((unsigned char)out[n]<32 || (unsigned char)out[n]==127)return fail(P28_TEXT_INVALID);
    out[name.length]=0;InterlockedIncrement64(&g_p273_definition_ok[i]);p28_sample(side,P28_NAMED,context,name);return true;
}
static void p273_merge_class(AnymakerVehicleComponentSide side,char existing[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX],const char* native_class,const P28Context* context=nullptr){
    if(!native_class)return;
    size_t i=p273_side_index(side);
    if(existing[0]){
        if(std::strcmp(existing,native_class)==0)InterlockedIncrement64(&g_p273_metadata_match[i]);
        else{InterlockedIncrement64(&g_p273_metadata_mismatch[i]);GeoString16 unused{};p28_sample(side,P28_NAME_CONFLICT,context?*context:P28Context{},unused,existing,native_class);}
        return;
    }
    if(!native_class[0])return; // known empty stays empty, distinct from an unreadable definition
    std::strncpy(existing,native_class,ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX-1);existing[ANYMAKER_COMPONENT_CLASS_SNAPSHOT_MAX-1]=0;InterlockedIncrement64(&g_p273_metadata_filled[i]);
}
