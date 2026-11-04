// Object parameters (aid_objparams_): one fixed struct per object class,
//   { u16 size, char objTag[64], char objDefId[64], fields... }
// whose fields are set by the objDefId class. Vehicle parts (objDefId_vehicleBlock*) share a
// 960-byte base read by the game at 0x82607A38; each part class appends its own fields.
#pragma once

#include "gamescript.hpp"

namespace nut::objparams {

bool isObjParams(const Bytes& d);
std::string className(const Bytes& d); // "objDefId_vehicleBlockEngine"
bool isVehiclePart(const Bytes& d);

// Editable text, one field per line: `mass = 30.0`. Fields without a name are written by offset
// (`0x3C4 = 1.5`); words that are 0 are left out.
std::string toText(const Bytes& d, const gamescript::NameLookup& names = {});
// Applies `text` onto `base`: the fields it lists change, every other byte stays. The size never changes.
Bytes fromText(const Bytes& base, std::string_view text);

} // namespace nut::objparams
