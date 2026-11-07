extern "C" {
    #include "kronklab/kronklab.h"
}
#include "Json.hpp"
#include "Localization.hpp"
#include <filesystem>
#include <fstream>

// NOTE: kronklab test names are limited to 31 characters.

namespace
{
    kuge::Localization twoLanguages(void)
    {
        kuge::Localization texts;

        texts.add("en", R"({"language.name":"English","play":"Play","hello":"Hello {0}","only.en":"Only","coins.one":"{0} coin","coins.other":"{0} coins"})");
        texts.add("fr", R"({"language.name":"Français","play":"Jouer","hello":"Bonjour {0}","coins.one":"{0} pièce","coins.other":"{0} pièces"})");
        return texts;
    }
}

Test(localization, switch_and_fallback)
{
    kuge::Localization texts = twoLanguages();

    AssertStrEq(texts.text("play").c_str(), "Play", "english first");
    Assert(texts.setLanguage("fr") && texts.language() == "fr", "the switch");
    AssertStrEq(texts.text("play").c_str(), "Jouer", "french");
    AssertStrEq(texts.text("only.en").c_str(), "Only", "a text that French lacks is the English one");
    AssertStrEq(texts.text("nothing.here").c_str(), "nothing.here", "and with none, the key");
    Assert(!texts.setLanguage("de") && texts.language() == "fr", "an unknown language changes nothing");
    AssertStrEq(texts.languageName("fr").c_str(), "Français", "a language says its own name");
    AssertEq(texts.languages().size(), 2u, "the languages");
    AssertStrEq(texts.format("hello", {"Ana"}).c_str(), "Bonjour Ana", "a value goes in");
    AssertStrEq(texts.format("hello", {}).c_str(), "Bonjour {0}", "a value that is not given stays as it is");
}

Test(localization, plural_forms)
{
    using kuge::PluralForm;

    Assert(kuge::pluralForm("en", 1) == PluralForm::One && kuge::pluralForm("en", 0) == PluralForm::Other, "English");
    Assert(kuge::pluralForm("fr", 0) == PluralForm::One && kuge::pluralForm("fr", 2) == PluralForm::Other, "French counts 0 as one");
    Assert(kuge::pluralForm("ja", 1) == PluralForm::Other, "Japanese has one form");
    Assert(kuge::pluralForm("ru", 21) == PluralForm::One && kuge::pluralForm("ru", 3) == PluralForm::Few && kuge::pluralForm("ru", 12) == PluralForm::Many
        && kuge::pluralForm("ru", 11) == PluralForm::Many, "Russian");
    Assert(kuge::pluralForm("pl", 1) == PluralForm::One && kuge::pluralForm("pl", 22) == PluralForm::Few && kuge::pluralForm("pl", 5) == PluralForm::Many, "Polish");
    Assert(kuge::pluralForm("cs", 3) == PluralForm::Few && kuge::pluralForm("cs", 5) == PluralForm::Other, "Czech");
    Assert(kuge::pluralForm("ar", 0) == PluralForm::Zero && kuge::pluralForm("ar", 2) == PluralForm::Two && kuge::pluralForm("ar", 7) == PluralForm::Few
        && kuge::pluralForm("ar", 25) == PluralForm::Many && kuge::pluralForm("ar", 100) == PluralForm::Other, "Arabic has all six");
    Assert(kuge::pluralForm("pt-BR", 1) == PluralForm::One, "a region is the language");
    kuge::Localization texts = twoLanguages();

    AssertStrEq(texts.plural("coins", 1).c_str(), "1 coin", "one");
    AssertStrEq(texts.plural("coins", 5).c_str(), "5 coins", "other");
    texts.setLanguage("fr");
    AssertStrEq(texts.plural("coins", 0).c_str(), "0 pièce", "French 0");
    AssertStrEq(texts.plural("coins", 2).c_str(), "2 pièces", "French 2");
}

Test(localization, load_a_folder)
{
    const std::filesystem::path folder = std::filesystem::temp_directory_path() / "kuge_localization_test";

    std::filesystem::remove_all(folder);
    std::filesystem::create_directories(folder);
    std::ofstream(folder / "en.json") << R"({"play":"Play"})";
    std::ofstream(folder / "fr.json") << R"({"play":"Jouer"})";
    kuge::Localization texts;

    texts.load(folder);
    texts.setLanguage("fr");
    AssertStrEq(texts.text("play").c_str(), "Jouer", "the file name is the language");
    std::ofstream(folder / "de.json") << R"({"play": 3})";
    bool named = false;

    try {
        kuge::Localization bad;

        bad.load(folder);
    } catch (const kuge::JsonError& error) {
        named = std::string(error.what()).find("de.json") != std::string::npos && std::string(error.what()).find("'play'") != std::string::npos;
    }
    Assert(named, "a bad file names itself and the key");
    std::filesystem::remove_all(folder);
}

Test(localization, problems)
{
    kuge::Localization texts = twoLanguages();
    const auto found = texts.problems();

    AssertEq(found.size(), 1u, "only 'only.en' differs");
    Assert(found[0].find("fr: lacks 'only.en'") == 0, "%s", found[0].c_str());
    texts.add("de", R"({"play":"Spielen {0}","hello":"Hallo","coins.one":"{0} Münze","only.en":"x","language.name":"Deutsch"})");
    const auto more = texts.problems();
    bool placeholder = false, other = false;

    for (const auto& problem : more) {
        placeholder |= problem.find("de: 'play' does not have the same") == 0;
        other |= problem.find("de: 'coins' has no form 'other'") == 0;
    }
    Assert(placeholder, "a {n} that English has not");
    Assert(other, "a plural with no 'other'");
}
