#pragma once
#include <windows.h>
#include "anyapi_item_catalog_v1.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Presentation only: the shared catalog, definition IDs and native Add requests
// keep their original meaning. Game translations are read locally, never bundled.
namespace inventory_localization {
inline std::wstring wide(std::string_view value) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),nullptr,0);
    if(!n)return {};
    std::wstring out(n,L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,value.data(),int(value.size()),out.data(),n);
    return out;
}
inline std::string utf8(std::wstring_view value) {
    int n=WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),nullptr,0,nullptr,nullptr);
    if(!n)return {};
    std::string out(n,'\0');
    WideCharToMultiByte(CP_UTF8,0,value.data(),int(value.size()),out.data(),n,nullptr,nullptr);
    return out;
}
inline std::string fold(std::string_view value) {
    auto source=wide(value);
    if(source.empty())return {};
    int n=LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,source.data(),int(source.size()),nullptr,0,nullptr,nullptr,0);
    if(!n)return std::string(value);
    std::wstring result(n,L'\0');
    LCMapStringEx(LOCALE_NAME_INVARIANT,LCMAP_LOWERCASE,source.data(),int(source.size()),result.data(),n,nullptr,nullptr,0);
    // Russian keyboards commonly omit the dots in a search, e.g. колеса/колёса.
    std::replace(result.begin(),result.end(),L'ё',L'е');
    return utf8(result);
}
inline bool matches(std::string_view haystack,std::string_view query) {
    size_t start=0;
    while(start<query.size()) {
        start=query.find_first_not_of(" \t\r\n",start);
        if(start==query.npos)break;
        auto end=query.find_first_of(" \t\r\n",start);
        auto word=query.substr(start,end==query.npos?query.size()-start:end-start);
        if(haystack.find(word)==haystack.npos)return false;
        if(end==query.npos)break;
        start=end+1;
    }
    return true;
}
inline std::string language_code(std::string_view steam) {
    // Steam API names -> Anymaker TSV headers (Brazilian Portuguese uses pt).
    static constexpr std::pair<std::string_view,std::string_view> codes[]={
        {"english","en"},{"french","fr"},{"german","de"},{"spanish","es"},
        {"japanese","ja"},{"brazilian","pt"},{"russian","ru"},{"schinese","zh"},
        {"bulgarian","bg"},{"tchinese","zh-TW"},{"czech","cs"},{"danish","da"},
        {"dutch","nl"},{"finnish","fi"},{"greek","el"},{"hungarian","hu"},
        {"indonesian","id"},{"italian","it"},{"koreana","ko"},{"malay","ms"},
        {"norwegian","no"},{"polish","pl"},{"portuguese","pt-PT"},{"romanian","ro"},
        {"latam","es-419"},{"swedish","sv"},{"thai","th"},{"turkish","tr"},
        {"ukrainian","uk"},{"vietnamese","vi"}
    };
    for(auto [name,code]:codes)if(steam==name)return std::string(code);
    return "en";
}
inline std::string steam_language() {
    // Borrow the game's initialized Steam client. Do not load, initialize, run
    // callbacks or shut down Steam from a mod. An empty result can be retried.
    auto module=GetModuleHandleW(L"steam_api64.dll");
    if(!module)return {};
    auto user=reinterpret_cast<int(__cdecl*)()>(GetProcAddress(module,"SteamAPI_GetHSteamUser"));
    auto apps=reinterpret_cast<void*(__cdecl*)()>(GetProcAddress(module,"SteamAPI_SteamApps_v008"));
    auto language=reinterpret_cast<const char*(__cdecl*)(void*)>(GetProcAddress(module,"SteamAPI_ISteamApps_GetCurrentGameLanguage"));
    if(!user||!apps||!language||!user())return {};
    auto client=apps();
    if(!client)return {};
    auto value=language(client);
    return value?value:"";
}

using Table=std::map<std::string,std::string,std::less<>>;
inline std::vector<std::string_view> cells(std::string_view row) {
    if(!row.empty()&&row.back()=='\r')row.remove_suffix(1);
    std::vector<std::string_view> out;
    size_t start=0;
    for(;;) {
        auto end=row.find('\t',start);
        out.push_back(row.substr(start,end==row.npos?row.size()-start:end-start));
        if(end==row.npos)break;
        start=end+1;
    }
    return out;
}
inline Table parse_table(std::string_view bytes,std::string_view language) {
    Table out;
    if(bytes.starts_with("\xef\xbb\xbf"))bytes.remove_prefix(3);
    auto end=bytes.find('\n');
    auto header=cells(bytes.substr(0,end));
    if(header.size()<3||header[0]!="id"||header[1]!="context")return out;
    size_t column=header.size(),english=header.size();
    // Column 0 is the record ID. A SECOND column named "id" is Indonesian.
    for(size_t i=2;i<header.size();++i) {
        if(header[i]==language)column=i;
        if(header[i]=="en")english=i;
    }
    while(end!=bytes.npos) {
        bytes.remove_prefix(end+1);
        end=bytes.find('\n');
        auto row=cells(bytes.substr(0,end));
        if(row.empty()||row[0].empty())continue;
        std::string_view value;
        if(column<row.size())value=row[column];
        auto valid=[](std::string_view text){return !text.empty()&&text.find('\0')==text.npos&&!wide(text).empty();};
        if(!valid(value)&&english<row.size())value=row[english];
        if(valid(value))
            out.emplace(std::string(row[0]),std::string(value));
    }
    return out;
}
inline Table read_table(const std::filesystem::path& path,std::string_view language) {
    std::ifstream in(path,std::ios::binary|std::ios::ate);
    if(!in)return {};
    auto size=in.tellg();
    if(size<=0||size>32*1024*1024)return {};
    std::string bytes(size_t(size),'\0');
    in.seekg(0);
    if(!in.read(bytes.data(),size))return {};
    return parse_table(bytes,language);
}
inline std::string lookup(const Table& table,std::string_view id,std::string_view fallback) {
    auto found=table.find(id);
    return found==table.end()?std::string(fallback):found->second;
}
inline std::string category_key(std::string_view category) {
    for(auto key:{"hat","body","back","legs","feet","head","eyes","ears","neck","body_over","hands"})
        if(category==key)return "item_category_wearable_"+std::string(category);
    if(category=="miscellaneous")return "item_category_none";
    for(auto key:{"medical","accessory","paint","vehicle_editor","weapon","storage"})
        if(category==key)return "item_category_item_"+std::string(category);
    return "item_category_component_"+std::string(category);
}
inline std::string category_fallback(std::string category) {
    std::replace(category.begin(),category.end(),'_',' ');
    if(!category.empty()&&category[0]>='a'&&category[0]<='z')category[0]-=32;
    return category;
}

enum class Text {
    title,search,all_categories,favorites,items,clear,catalog_unavailable,no_matches,
    search_all,no_description,add,hide,save_failed,requesting,add_unavailable,
    requested,cancelled,request_failed,toggle,enabled,width,side,search_descriptions,
    show_ids,right,left,food,drink
};
inline const char* text(Text key,std::string_view language) {
    static constexpr const char* en[]={
        "ITEM BROWSER","Search items","All categories","Favorites","items","Clear",
        "Catalog unavailable","No matching items","Search all categories","No description available.",
        "Add","hide browser","Could not save favorites","Requesting item...","Add is unavailable right now",
        "Item requested","Request cancelled; inventory changed","Could not request this item",
        "Show / hide item browser in inventory","Show item browser","Item browser width","Item browser side",
        "Search item descriptions","Show item IDs in list","Right","Left","Food","Drink"
    };
    static constexpr const char* ru[]={
        "ПРЕДМЕТЫ","Поиск предметов","Все категории","Избранное","предметов","Сброс",
        "Каталог недоступен","Ничего не найдено","Искать во всех категориях","Нет описания.",
        "Добавить","скрыть каталог","Не удалось сохранить избранное","Запрос предмета...","Добавление пока недоступно",
        "Запрос предмета отправлен","Запрос отменён: инвентарь изменился","Не удалось запросить предмет",
        "Показать / скрыть каталог в инвентаре","Показывать каталог предметов","Ширина каталога","Расположение каталога",
        "Искать в описаниях предметов","Показывать ID предметов","Справа","Слева","Еда","Напитки"
    };
    static_assert(std::size(en)==std::size(ru));
    return (language=="ru"?ru:en)[size_t(key)];
}
struct DisplayItem {
    std::string name,description,category,search,description_search,sort;
};
struct Localization {
    std::string language="en";
    Table names,descriptions,component_names,component_descriptions,categories;
    void load(const std::filesystem::path& game,std::string code) {
        language=std::move(code);
        auto rom=game/L"rom";
        names=read_table(rom/L"languages_items.tsv",language);
        descriptions=read_table(rom/L"languages_item_descriptions.tsv",language);
        component_names=read_table(rom/L"languages_components.tsv",language);
        component_descriptions=read_table(rom/L"languages_component_descriptions.tsv",language);
        categories=read_table(rom/L"languages.tsv",language);
    }
    const char* text(Text key)const{return inventory_localization::text(key,language);}
    std::string category(std::string_view key)const {
        // The game combines these categories; the browser has two distinct filters.
        if(key=="food")return text(Text::food);
        if(key=="drink")return text(Text::drink);
        // Preserve the familiar English category labels for existing users.
        auto fallback=category_fallback(std::string(key));
        return language=="en"?fallback:lookup(categories,category_key(key),fallback);
    }
    DisplayItem item(const AnyItemDefinitionV1& source)const {
        std::string_view id=source.id;
        bool component=(source.valid_fields&ANY_ITEM_VEHICLE_COMPONENT)&&id.starts_with("component:");
        if(component)id.remove_prefix(10);
        DisplayItem out;
        out.name=lookup(component?component_names:names,id,source.name);
        out.description=lookup(component?component_descriptions:descriptions,id,source.description);
        out.category=category(source.category);
        out.sort=fold(out.name);
        out.search=fold(out.name+" "+out.category+" "+source.name+" "+source.id+" "+source.category+" "+source.item_class);
        out.description_search=fold(out.description+" "+source.description);
        return out;
    }
};
}
