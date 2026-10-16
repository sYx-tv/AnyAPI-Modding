#define NOMINMAX
#include "inventory_localization.h"
#include "anyapi_item_catalog.h"
#include <cassert>
#include <iostream>
using namespace inventory_localization;

int wmain(int argc,wchar_t** argv) {
    assert(language_code("russian")=="ru");
    assert(language_code("indonesian")=="id");
    assert(language_code("brazilian")=="pt");
    assert(language_code("portuguese")=="pt-PT");
    assert(language_code("tchinese")=="zh-TW");
    assert(language_code("latam")=="es-419");
    assert(language_code("unknown")=="en");
    const std::string tsv="\xef\xbb\xbf" "id\tcontext\ten\tru\tid\r\n"
        "test\tcomment\tEnglish\tРусский\tIndonesia\r\n"
        "blank\t\tFallback\t\t\r\n"
        "empty\t\t\t\t\r\n"
        "short\t\tShort\n"
        "last\t\tLast\tПоследний\tAkhir";
    auto ru=parse_table(tsv,"ru"),id=parse_table(tsv,"id");
    assert(ru.at("test")=="Русский"&&id.at("test")=="Indonesia");
    assert(!id.contains("Indonesia")); // Indonesian text must never become an ID.
    assert(ru.at("blank")=="Fallback"&&ru.at("short")=="Short");
    assert(!ru.contains("empty")&&ru.at("last")=="Последний");
    assert(parse_table(tsv,"zz").at("test")=="English");
    assert(parse_table("bad\theader\nrow\tvalue","ru").empty());
    assert(parse_table("id\tcontext\ten\tru\nx\t\tOK\t\xff","ru").at("x")=="OK");
    assert(fold("ДВИГАТЕЛЬ ENGINE")=="двигатель engine");
    assert(fold("КОЛЁСА")==fold("колеса"));
    assert(fold("ÉCLAIR Ä Ö Ü")==fold("éclair ä ö ü"));
    assert(fold("日本語")=="日本語");
    assert(matches(fold("Двигатель component:engine small"),fold("  SMALL  ДВИГ\t")));
    assert(!matches(fold("Двигатель"),fold("двигатель bandage")));

    Localization loc;loc.language="ru";
    loc.names["same"]="Предмет";loc.component_names["same"]="Компонент";
    loc.component_descriptions["same"]="Текст описания";
    loc.categories["item_category_component_engine"]="ДВИГАТЕЛИ";
    AnyItemDefinitionV1 item;
    strcpy_s(item.id,"component:same");strcpy_s(item.name,"Part");
    strcpy_s(item.description,"English description");strcpy_s(item.category,"engine");
    item.valid_fields|=ANY_ITEM_VEHICLE_COMPONENT;
    auto display=loc.item(item);
    assert(display.name=="Компонент"&&display.description=="Текст описания"&&display.category=="ДВИГАТЕЛИ");
    assert(matches(display.search,fold("КОМП part component:same")));
    assert(!matches(display.search,fold("описания")));
    assert(matches(display.search+" "+display.description_search,fold("описания ENGLISH")));
    item.valid_fields=0;strcpy_s(item.id,"same");
    assert(loc.item(item).name=="Предмет");
    strcpy_s(item.id,"modded_item");
    assert(loc.item(item).name=="Part"&&loc.item(item).description=="English description");
    assert(loc.category("new_category")=="New category");
    assert(loc.category("food")=="Еда"&&loc.category("drink")=="Напитки");
    assert(std::string(loc.text(Text::add))=="Добавить");
    loc.language="fr";assert(std::string(loc.text(Text::add))=="Add");

    if(argc==3&&std::wstring(argv[1])==L"--steam") {
        assert(steam_language().empty());
        auto module=LoadLibraryW(argv[2]);assert(module);
        auto set=reinterpret_cast<void(*)(int)>(GetProcAddress(module,"FixtureSteamReady"));assert(set);
        assert(steam_language().empty()); // Loaded, but the game has not initialized Steam.
        set(1);assert(steam_language()=="russian");
        set(0);assert(steam_language().empty());
        FreeLibrary(module);assert(steam_language().empty());
    } else if(argc==2) {
        loc.load(argv[1],"ru");
        auto catalog=itemcatalog::load(argv[1]);
        assert(loc.names.size()==372&&loc.component_names.size()==598&&catalog.size()==970);
        size_t named=0;
        for(auto& entry:catalog) {
            const auto& source=entry.metadata;auto translated=loc.item(source);
            assert(!translated.name.empty()&&!wide(translated.name).empty());
            assert(matches(translated.search,fold(source.id)));
            assert(matches(translated.search,fold(source.name)));
            if(translated.name!=source.name)++named;
            if(std::string(source.id)=="component:engine")assert(translated.name=="Двигатель");
            if(std::string(source.id)=="bandage")assert(translated.name=="Бинт");
        }
        assert(named>900);
        loc.load(argv[1],"id");
        assert(loc.names.size()==372&&loc.names.contains("overalls"));
        loc.load(std::filesystem::path(argv[1])/L"missing-localization-fixture","ru");
        assert(loc.names.empty()&&loc.item(catalog.front().metadata).name==catalog.front().metadata.name);
        std::cout<<"PASS: current game translations, "<<named<<" localized names, English aliases and missing-file fallback\n";
    }
    std::cout<<"PASS: localization tables, language mapping, Unicode search and stable IDs\n";
}
