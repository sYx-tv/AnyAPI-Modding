#pragma once
#include <cstdint>
// Generic, read-only authored definitions; these are not owned inventory instances.
// Query anyapi.item_catalog version 1 after AnyAPI_ModReady. No native pointers.
enum AnyItemMetadataFlags:uint32_t {ANY_ITEM_SIZE=1,ANY_ITEM_TECH_TIER=2,ANY_ITEM_DEFAULT_STACK=4,ANY_ITEM_VEHICLE_COMPONENT=8};
struct AnyItemDefinitionV1 {uint32_t struct_size{sizeof(AnyItemDefinitionV1)},version{1},valid_fields{},reserved{};char id[128]{},name[192]{},description[2048]{},item_class[96]{},category[96]{};int32_t width{},height{},tech_tier{},default_stack{};};
struct AnyItemCatalogV1 {uint32_t struct_size{sizeof(AnyItemCatalogV1)},version{1};uint32_t (*count)(){};bool (*copy)(uint32_t index,AnyItemDefinitionV1*){};bool (*definition_json)(uint32_t index,char* bytes,uint32_t capacity,uint32_t* required){};};
// definition_json reports required bytes including NUL. Insufficient buffers receive no partial JSON.
// Metadata is English (authored English localization, then definition fallback). Catalog is immutable
// for this process and refreshed from the installed game's assets on every launch.
