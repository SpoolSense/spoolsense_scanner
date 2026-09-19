#include "TigerTagParser.h"
#include <cstring>

// ─── Lookup tables from TigerTag database ────────────────────────────────────
// Source: https://github.com/TigerTag-Project/TigerTag-RFID-Guide/tree/main/database

struct IdNameEntry {
    uint16_t id;
    const char* name;
};

// Material IDs from TigerTag database (integer IDs, stored as uint16_t)
static const IdNameEntry MATERIALS[] = {
    {425, "ABS-CF"},       {735, "ABS-AF"},       {1173, "PA6-GF"},
    {2053, "PA12-GF"},     {3368, "PC-ABS"},      {3481, "PCTG-GF"},
    {4587, "PC-PBT"},      {5733, "TPU-AMS"},     {7649, "PETG-HS"},
    {7951, "PETG-rCF"},    {8345, "PLA+ Silk"},   {8394, "Cast Resin"},
    {8504, "PPA-CF"},      {9456, "PLA Marble"},  {9483, "PVA"},
    {9691, "EVA"},         {10187, "PHA"},        {10272, "PSU"},
    {10478, "Cast Fil"},   {10602, "PLA Silk"},   {10738, "PC-PTFE"},
    {11053, "PET-CF"},     {11506, "PLA-LW"},     {12264, "PA6-CF"},
    {12844, "ASA"},        {13850, "PPA"},        {15041, "PCTG"},
    {18130, "PS"},         {18703, "PETP"},       {18775, "PE-CF"},
    {18922, "PLA-ESD"},    {20073, "PVC"},        {20562, "ABS"},
    {24115, "SEBS"},       {24116, "TPC"},        {24270, "PPS-CF"},
    {24629, "PLA-HS"},     {26029, "HIPS"},       {27268, "PCTPE"},
    {27635, "PE"},         {27676, "ASA-CF"},     {28110, "SBC"},
    {29815, "PEEK"},       {30458, "PC"},         {30594, "PA-GF"},
    {30884, "PP"},         {31011, "ASA-LW"},     {33958, "TPE"},
    {34049, "BVOH"},       {34409, "TPS"},        {35100, "ASA-GF"},
    {38219, "PLA"},        {38256, "PETG"},       {39667, "PA12-CF"},
    {39944, "PA-CF"},      {42623, "PMMA"},       {42962, "PP-GF"},
    {43518, "TPU"},        {45962, "PVB"},        {46154, "PPS"},
    {46276, "PPA-GF"},     {46591, "PLA+"},       {47651, "PC-PBT-CF"},
    {48001, "PLA Wood"},   {48047, "TPU-HS"},     {48310, "PLA-CF"},
    {48815, "PAHT-CF"},    {49074, "ABS-GF"},     {49152, "PPSU"},
    {49804, "ASA-AF"},     {50206, "POM"},        {50497, "PP-CF"},
    {51007, "Biopolymer"}, {51861, "PETG-ESD"},   {52077, "PET"},
    {53890, "PCTG-CF"},    {53970, "PEKK"},       {54568, "ASA+"},
    {55279, "PBT"},        {55418, "PETG-CF"},    {55796, "PA12"},
    {56527, "PEI"},        {56666, "PA6"},        {57469, "PETG-HF"},
    {58142, "TPU-GF"},     {58498, "PEBA"},       {59328, "PA"},
    {61048, "PVDF"},       {61563, "PC-PBT-GF"},  {63946, "TPI"},
    {65535, "None"},
};
static constexpr size_t MATERIALS_COUNT = sizeof(MATERIALS) / sizeof(MATERIALS[0]);

// Brand IDs from TigerTag database (integer IDs, stored as uint16_t)
static const IdNameEntry BRANDS[] = {
    {1, "Atome3D"},          {2, "Proto-Pasta"},      {1421, "3DJake"},
    {2517, "Smart Mat 3D"},  {2833, "Xstrand"},       {3132, "Hatchbox"},
    {4011, "QIDI Tech"},     {4048, "Owa"},           {4344, "MatterHackers"},
    {4356, "Landu"},         {7674, "Extrudr"},       {7812, "Jayo"},
    {7980, "Fillamentum"},   {8182, "Fiberlogy"},     {8303, "GST3D"},
    {8384, "Taulman3D"},     {8586, "NinjaTek"},      {8675, "SOVB 3D"},
    {8756, "BlueCast"},      {8990, "Ice Filaments"}, {9192, "3D Solutech"},
    {9394, "Gizmo Dorks"},   {9596, "Ziro"},          {9798, "AMOLEN"},
    {11429, "3D4Makers"},    {11501, "InnovateFil"},  {12345, "MakerBot"},
    {12498, "Forshape"},     {14982, "3D-Fuel"},      {15899, "Kimya"},
    {15962, "Anycubic"},     {18629, "PrintoMax 3D"}, {19265, "CC3D"},
    {19961, "Rosa3D"},       {20523, "Raise3D"},      {20851, "Tronxy"},
    {22652, "Spectrum"},     {23181, "ArianePlast"},   {23456, "Monoprice"},
    {26595, "Sovol"},        {26956, "Creality"},      {28055, "TAGin3D"},
    {28136, "Polar Fil"},    {28940, "Eryone"},       {29045, "Yousu"},
    {29302, "IIIDMAX"},      {32587, "Amazon"},       {33788, "Verbatim"},
    {34567, "Push Plastic"}, {35123, "Bambu Lab"},    {36702, "Tianse"},
    {37434, "Winkle"},       {39382, "Longer"},       {39652, "3DXTech"},
    {41932, "Jamg He"},      {45670, "Panchroma"},    {45678, "Atomic Fil"},
    {46010, "AceAddity"},    {46203, "Overture"},     {46392, "Prusa"},
    {47560, "Wanhao"},       {47930, "eSun"},         {48804, "R3D"},
    {49784, "GIANTARN"},     {50311, "G3D Pro"},      {50604, "Polymaker"},
    {51443, "BASF"},         {51857, "Sunlu"},        {52222, "ColorFabb"},
    {52467, "Geeetech"},     {52757, "Yumi"},         {53043, "FormFutura"},
    {53640, "Magigoo"},      {53856, "Lattice Med"},  {54112, "Kexcelled"},
    {55763, "Nanovia"},      {55869, "Biqu"},         {56780, "Fiberon"},
    {56789, "Coex 3D"},      {57209, "FrancoFil"},    {57632, "Elegoo"},
    {58231, "IC3D"},         {58410, "AzureFilm"},    {60882, "Recreus"},
    {63340, "Flashforge"},   {65535, "Generic"},
};
static constexpr size_t BRANDS_COUNT = sizeof(BRANDS) / sizeof(BRANDS[0]);

struct AspectEntry {
    uint8_t id;
    const char* name;
};

// Aspect IDs from TigerTag database
static const AspectEntry ASPECTS[] = {
    {0,   "-"},           {21,  "Clear"},       {24,  "Tricolor"},
    {64,  "Glitter"},     {67,  "Translucent"}, {91,  "Glow"},
    {92,  "Silk"},        {97,  "Lithophane"},   {104, "Basic"},
    {123, "Wood"},        {126, "Pearl"},        {129, "Gloss"},
    {134, "Satin"},       {145, "Rainbow"},      {168, "Thermoreactif"},
    {173, "Stone"},       {216, "Neon"},         {220, "Pastel"},
    {232, "Marble"},      {238, "Carbon"},       {247, "Matt"},
    {252, "Bicolor"},     {255, "None"},
};
static constexpr size_t ASPECTS_COUNT = sizeof(ASPECTS) / sizeof(ASPECTS[0]);

// Measure unit IDs from TigerTag database
struct UnitEntry {
    uint8_t id;
    const char* name;
};

static const UnitEntry UNITS[] = {
    {10, "mg"}, {21, "g"},  {35, "kg"},
    {48, "ml"}, {62, "cl"}, {79, "L"},
    {95, "m3"}, {112, "mm"}, {130, "cm"},
    {149, "m"}, {170, "m2"},
};
static constexpr size_t UNITS_COUNT = sizeof(UNITS) / sizeof(UNITS[0]);

// ─── Lookup functions ────────────────────────────────────────────────────────

const char* tigerTagMaterialName(uint16_t id) {
    for (size_t i = 0; i < MATERIALS_COUNT; i++) {
        if (MATERIALS[i].id == id) return MATERIALS[i].name;
    }
    return "Unknown";
}

const char* tigerTagBrandName(uint16_t id) {
    for (size_t i = 0; i < BRANDS_COUNT; i++) {
        if (BRANDS[i].id == id) return BRANDS[i].name;
    }
    return "Unknown";
}

const char* tigerTagAspectName(uint8_t id) {
    for (size_t i = 0; i < ASPECTS_COUNT; i++) {
        if (ASPECTS[i].id == id) return ASPECTS[i].name;
    }
    return "Unknown";
}

float tigerTagDiameterMm(uint8_t id) {
    switch (id) {
        case 56:  return 1.75f;   // ID 56 = 1.75mm
        case 221: return 2.85f;   // ID 221 = 2.85mm
        case 0:   return 0.0f;    // Not specified
        default:  return 0.0f;
    }
}

const char* tigerTagUnitName(uint8_t id) {
    for (size_t i = 0; i < UNITS_COUNT; i++) {
        if (UNITS[i].id == id) return UNITS[i].name;
    }
    return "?";
}

// ─── Magic check ─────────────────────────────────────────────────────────────

bool tigerTagHasKnownVersion(const uint8_t* data, uint16_t dataLen) {
    if (data == nullptr || dataLen < 4) return false;
    uint32_t versionId = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
                         ((uint32_t)data[2] << 8) | data[3];
    return versionId == TIGERTAG_V10 ||
           versionId == TIGERTAG_V10_MAKER ||
           versionId == TIGERTAG_INIT_V10 ||
           versionId == TIGERTAG_PLUS_V10;
}

bool tigerTagCheckMagic(const uint8_t* data, uint16_t dataLen) {
    if (dataLen < 14) return false;  // Need at least through Type field

    if (tigerTagHasKnownVersion(data, dataLen)) return true;

    // Permissive mode: unknown version ID but layout looks like TigerTag.
    // Check Type field (byte 12) = Filament or Resin, and Material (bytes 8-9) is non-zero.
    // TigerTag's docs/app may produce version IDs not yet in the public database.
    // Byte 12 is the type field; byte 13 is diameter (see the layout map below).
    uint8_t typeField = data[12];
    uint16_t materialId = (data[8] << 8) | data[9];
    if ((typeField == TIGERTAG_TYPE_FILAMENT || typeField == TIGERTAG_TYPE_RESIN) && materialId != 0) {
        return true;
    }

    return false;
}

// ─── Parser ──────────────────────────────────────────────────────────────────

// Byte layout (offsets relative to page 4, i.e. data[0] = page 4 byte 0):
// Derived from comparing TigerTag app output against raw tag data.
// NOTE: This differs from the spec README which has incorrect field ordering.
//
//  0-3:   ID TigerTag (version)
//  4-7:   ID Product (0xFFFFFFFF = Maker/Offline)
//  8-9:   ID Material (big-endian uint16)
//  10:    ID Aspect 1
//  11:    ID Aspect 2
//  12:    ID Type (142 = Filament, 173 = Resin)
//  13:    ID Diameter (56 = 1.75mm, 221 = 2.85mm)
//  14-15: ID Brand (big-endian uint16)
//  16-19: Color1 RGBA
//  20-22: Measure (big-endian 3 bytes, weight in grams)
//  23:    ID Unit (21 = g)
//  24-25: Nozzle Temp Min (big-endian uint16, °C)
//  26-27: Nozzle Temp Max (big-endian uint16, °C)
//  28:    Dry Temp (uint8, °C)
//  29:    Dry Time (uint8, hours)
//  30:    Bed Temp Min (uint8, °C)
//  31:    Bed Temp Max (uint8, °C)

TigerTagData tigerTagParse(const uint8_t* data, uint16_t dataLen) {
    TigerTagData result;
    memset(&result, 0, sizeof(result));

    if (dataLen < 32 || !tigerTagCheckMagic(data, dataLen)) {
        result.valid = false;
        return result;
    }

    result.valid = true;

    // Material ID (big-endian uint16 at offset 8)
    result.material_id = (data[8] << 8) | data[9];
    result.material_name = tigerTagMaterialName(result.material_id);

    // Aspects
    result.aspect1_id = data[10];
    result.aspect1_name = tigerTagAspectName(result.aspect1_id);
    result.aspect2_id = data[11];
    result.aspect2_name = tigerTagAspectName(result.aspect2_id);

    // Type
    result.type_id = data[12];

    // Diameter
    result.diameter_id = data[13];
    result.diameter_mm = tigerTagDiameterMm(result.diameter_id);

    // Brand ID (big-endian uint16 at offset 14)
    result.brand_id = (data[14] << 8) | data[15];
    result.brand_name = tigerTagBrandName(result.brand_id);

    // Primary color RGBA (offset 16)
    result.color_r = data[16];
    result.color_g = data[17];
    result.color_b = data[18];
    result.color_a = data[19];

    // Measure / weight (big-endian 3 bytes at offset 20)
    result.weight_g = ((uint32_t)data[20] << 16) | ((uint32_t)data[21] << 8) | data[22];

    // Unit
    result.unit_id = data[23];

    // Nozzle temps (big-endian uint16)
    result.nozzle_temp_min = (data[24] << 8) | data[25];
    result.nozzle_temp_max = (data[26] << 8) | data[27];

    // Dry
    result.dry_temp = data[28];
    result.dry_time_hours = data[29];

    // Bed temps
    result.bed_temp_min = data[30];
    result.bed_temp_max = data[31];

    return result;
}
