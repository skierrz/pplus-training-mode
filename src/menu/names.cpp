#include "menu/names.h"

#include <cstdio>

namespace tm {
namespace menu {
    // Indexed by gmCharacterKind (BrawlHeaders gm/gm_lib.h:68-136).
    // P+ changes 0x23 to "Dedede" (RSBE01.TXT:2795-2799), makes Giga Bowser
    // (0x2C), Wario-Man (0x2D) and Popo (0x11) selectable via Hold L
    // (ProjectM/CSS.ASM:150-198), and adds Roy (0x32), Mewtwo (0x33) and
    // Knuckles (0x35) (ProjectM/CloneEngine.asm:25-38, 215-225, 493-497).
    // NULL entries are non-CSS slots (Alloys 0x2E-0x31, unused 0x34).
    static const char* const kCharacters[] = {
        "Mario", "Donkey Kong", "Link", "Samus", "Zero Suit Samus", "Yoshi", "Kirby", "Fox", "Pikachu", "Luigi",
        "Captain Falcon", "Ness", "Bowser", "Peach", "Zelda", "Sheik", "Ice Climbers", "Popo", "Nana", "Marth",
        "Mr. Game & Watch", "Falco", "Ganondorf", "Wario", "Meta Knight", "Pit", "Olimar", "Lucas", "Diddy Kong",
        "Charizard", "Charizard", "Squirtle", "Squirtle", "Ivysaur", "Ivysaur", "Dedede", "Lucario", "Ike",
        "R.O.B.", "Jigglypuff", "Toon Link", "Wolf", "Snake", "Sonic", "Giga Bowser", "Wario-Man",
        NULL, NULL, NULL, NULL, "Roy", "Mewtwo", NULL, "Knuckles"
    };

    struct StageName {
        u8 kind;
        const char* name;
    };
    // Base stage names by srStageKind (BrawlHeaders gm/gm_lib.h:139-241).
    // P+ maps SSS entries to srStageKind and cosmetic id via
    // pf/STAGE/SWITCH/Switch00.rss (+0x104, Project+/StageFiles.asm:1398-1423)
    // and loads pf/STAGE/stageslot/%02X.ASL (Project+/StageFiles.asm:571-782).
    // Display names come from pf/MENU2/mu_menumain.pac MiscData[14]/MiscData[1]
    // at cosmeticId + 8 (Project+/Random.asm:466-468), with ASCII "e" for
    // accented letters. Alternate stages share the base srStageKind (the held
    // buttons are kept in altStage), so each id names its base stage.
    static const StageName kStages[] = {
        {0x01, "Battlefield"},         {0x02, "Final Destination"},  {0x03, "Delfino's Secret"},
        {0x04, "Luigi's Mansion"},     {0x05, "Metal Cavern"},       {0x06, "Bowser's Castle"},
        {0x07, "Kongo Jungle"},        {0x08, "Rumble Falls"},       {0x09, "Temple of Time"},
        {0x0B, "Norfair"},             {0x0C, "Frigate Husk"},       {0x0D, "Yoshi's Island"},
        {0x0E, "Halberd"},             {0x13, "Lylat Cruise"},       {0x14, "Saffron City"},
        {0x15, "Spear Pillar"},        {0x16, "Port Town Aero Dive"}, {0x17, "Infinite Glacier"},
        {0x18, "Flat Zone 2"},         {0x19, "Castle Siege"},       {0x1C, "Wario Land"},
        {0x1D, "Distant Planet"},      {0x1E, "Skyworld"},           {0x1F, "Fountain of Dreams"},
        {0x20, "Fourside"},            {0x21, "Smashville"},         {0x22, "Shadow Moses Island"},
        {0x23, "Green Hill Zone"},     {0x24, "PictoChat"},          {0x25, "Hanenbow"},
        {0x29, "Hyrule Temple"},       {0x2A, "Yoshi's Story"},      {0x2B, "Jungle Japes"},
        {0x2C, "Onett"},               {0x2D, "Dream Land"},         {0x2E, "Pokemon Stadium 2"},
        {0x2F, "Peach's Castle"},      {0x30, "Corneria"},           {0x31, "Big Blue"},
        {0x32, "Planet Zebes"},        {0x33, "Hyrule Castle"},      {0x35, "Stage Builder"},
        {0x37, "Training Room"},       {0x40, "Dracula's Castle"},   {0x41, "Mario Circuit"},
        {0x42, "Clock Town"},          {0x43, "Sky Sanctuary Zone"}, {0x44, "Dead Line"},
        {0x45, "Dinosaur Land"},       {0x46, "Minecart Madness"},   {0x47, "Golden Temple"},
        {0x48, "Bell Tower"},          {0x49, "Ceres Space Colony"}, {0x4A, "Cookie Country"},
        {0x4B, "Venus Lighthouse"},    {0x4C, "Mushroom Kingdom"},   {0x4D, "WarioWare, Inc."},
        {0x4E, "Subspace"},            {0x4F, "Rainbow Cruise"},     {0x50, "Poke Floats"},
        {0x51, "Pirate Ship"},         {0x52, "Venom"}
    };

    // Two of each, so both fighters of a title can be numbers.
    static char s_number[3][16];
    static int s_next = 0;

    static const char* number(const char* format, u32 kind)
    {
        char* out = s_number[s_next];
        s_next = (s_next + 1) % 3;
        snprintf(out, sizeof(s_number[0]), format, kind);
        return out;
    }

    const char* characterName(u32 kind)
    {
        if (kind < sizeof(kCharacters) / sizeof(kCharacters[0]) && kCharacters[kind] != NULL)
        {
            return kCharacters[kind];
        }
        return number("Fighter %u", kind);
    }

    const char* stageName(u32 kind)
    {
        for (u32 i = 0; i < sizeof(kStages) / sizeof(kStages[0]); i++)
        {
            if (kStages[i].kind == kind)
            {
                return kStages[i].name;
            }
        }
        return number("Stage %u", kind);
    }
}
}
