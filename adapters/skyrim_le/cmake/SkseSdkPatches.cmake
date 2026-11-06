include_guard(GLOBAL)

function(_skyrim_le_replace_exact file_path old_text new_text patch_id)
    set(_expected_count 1)
    if(ARGC GREATER 4)
        set(_expected_count "${ARGV4}")
    endif()
    file(READ "${file_path}" _source_text)
    string(REPLACE "\r\n" "\n" _source_text "${_source_text}")
    string(LENGTH "${_source_text}" _source_length)
    string(REPLACE "${old_text}" "" _without_match "${_source_text}")
    string(LENGTH "${_without_match}" _without_match_length)
    string(LENGTH "${old_text}" _old_length)
    math(EXPR _removed_length "${_source_length} - ${_without_match_length}")
    math(EXPR _expected_removed_length "${_old_length} * ${_expected_count}")
    if(_old_length EQUAL 0 OR NOT _removed_length EQUAL _expected_removed_length)
        message(FATAL_ERROR
            "SKSE compatibility patch ${patch_id} expected ${_expected_count} exact source matches in ${file_path}.")
    endif()
    string(REPLACE "${old_text}" "${new_text}" _patched_text "${_source_text}")
    file(WRITE "${file_path}" "${_patched_text}")
endfunction()

function(_skyrim_le_apply_compatibility_patches staging_root)
    set(_skyrim_le_sdk_source_root "${staging_root}")

    set(_skyrim_le_game_types "${_skyrim_le_sdk_source_root}/src/skse/skse/GameTypes.h")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_types}"
        "const _Node* FindString(char* str, Iterator prev) const\n\t{\n\t\treturn Find(StringFinder_CI(str), prev);\n\t}"
        "const _Node* FindString(char* str, Iterator prev) const = delete;"
        "LE-SKSE-001")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_types}"
        "Item* pRemoved = NULL;"
        "T* pRemoved = NULL;"
        "LE-SKSE-002")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_types}"
        "Item* pReplaced = NULL;"
        "T* pReplaced = NULL;"
        "LE-SKSE-003")

    set(_skyrim_le_game_events "${_skyrim_le_sdk_source_root}/src/skse/skse/GameEvents.h")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_events}"
        "virtual ~BSTEventSink();"
        "virtual ~BSTEventSink() = default;"
        "LE-SKSE-010")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_events}"
        "virtual\tEventResult\tReceiveEvent(T * evn, EventDispatcher<T> * dispatcher); // pure"
        "virtual\tEventResult\tReceiveEvent(T *, EventDispatcher<T> *) { return kEvent_Continue; }"
        "LE-SKSE-015")

    set(_skyrim_le_ni_types "${_skyrim_le_sdk_source_root}/src/skse/skse/NiTypes.h")
    _skyrim_le_replace_exact(
        "${_skyrim_le_ni_types}"
        "void NiTLargeArray::Append(T item)"
        "void Append(T item)"
        "LE-SKSE-004")
    _skyrim_le_replace_exact(
        "${_skyrim_le_ni_types}"
        "void NiTLargeArray::Resize(UInt32 size)"
        "void Resize(UInt32 size)"
        "LE-SKSE-005")

    set(_skyrim_le_game_data "${_skyrim_le_sdk_source_root}/src/skse/skse/GameData.cpp")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_data}"
        "return modList.modInfoList.Find(LoadedModFinder(modName));"
        "LoadedModFinder finder(modName);\n\treturn modList.modInfoList.Find(finder);"
        "LE-SKSE-006")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_data}"
        "return modList.modInfoList.GetIndexOf(LoadedModFinder(modName));"
        "LoadedModFinder finder(modName);\n\treturn modList.modInfoList.GetIndexOf(finder);"
        "LE-SKSE-007")

    set(_skyrim_le_game_settings "${_skyrim_le_sdk_source_root}/src/skse/skse/GameSettings.cpp")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_settings}"
        "const float\t* g_mapLocalHeight = *(float **)0x0124166C;\n"
        ""
        "LE-SKSE-008")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_settings}"
        "const float\t* g_localMapMinFrustumWidth = *(float **)0x012B5B80;\n"
        ""
        "LE-SKSE-009")

    set(_skyrim_le_game_extra_data "${_skyrim_le_sdk_source_root}/src/skse/skse/GameBSExtraData.cpp")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_extra_data}"
        "const char * BaseExtraList::GetDisplayName(TESForm * type)\n{\n\tfloat healthValue = 1.0;\n\n\tExtraHealth* xHealth = static_cast<ExtraHealth*>(GetByType(kExtraData_Health));\n\tif(xHealth)\n\t\thealthValue = xHealth->health;\n\n\tExtraTextDisplayData * xText = CALL_MEMBER_FN(this, GetExtraTextDisplayData_Internal)();\n\tif (!xText && healthValue != 1.0)\n\t{\n\t\txText = ExtraTextDisplayData::Create();\n\t\tAdd(kExtraData_TextDisplayData, xText);\n\t}\n\n\tif (xText)\n\t\treturn xText->GenerateName(type, healthValue);\n\telse\n\t\treturn NULL;\n}\n"
        ""
        "LE-SKSE-016")

    set(_skyrim_le_game_forms "${_skyrim_le_sdk_source_root}/src/skse/skse/GameForms.cpp")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_forms}"
        [[void TESForm::CopyFromEx(TESForm * rhsForm)
{
	if(!rhsForm || rhsForm->formType != formType)
		return;

	switch(formType)
	{
		case kFormType_CombatStyle:
		{
			TESCombatStyle	* lhs = (TESCombatStyle *)this;
			TESCombatStyle	* rhs = (TESCombatStyle *)rhsForm;

			lhs->general =				rhs->general;
			lhs->melee =				rhs->melee;
			lhs->closeRange =			rhs->closeRange;
			lhs->longRange =			rhs->longRange;
			lhs->flight =				rhs->flight;
			lhs->flags =				rhs->flags;
		}
		break;

		// Untested, probably shouldn't use this
		case kFormType_Armor:
		{
			TESObjectARMO	* lhs = (TESObjectARMO*)this;
			TESObjectARMO	* rhs = (TESObjectARMO*)rhsForm;

			lhs->fullName.CopyFromBase(&rhs->fullName);
			lhs->race.CopyFromBase(&rhs->race);
			lhs->enchantable.CopyFromBase(&rhs->enchantable);
			lhs->value.CopyFromBase(&rhs->value);
			lhs->weight.CopyFromBase(&rhs->weight);
			lhs->destructible.CopyFromBase(&rhs->destructible);
			lhs->pickupSounds.CopyFromBase(&rhs->pickupSounds);
			lhs->bipedModel.CopyFromBase(&rhs->bipedModel);
			lhs->equipType.CopyFromBase(&rhs->equipType);
			lhs->bipedObject.CopyFromBase(&rhs->bipedObject);
			lhs->blockBash.CopyFromBase(&rhs->blockBash);
			lhs->keyword.CopyFromBase(&rhs->keyword);
			lhs->description.CopyFromBase(&rhs->description);

			lhs->armorValTimes100	= rhs->armorValTimes100;
			lhs->armorAddons.CopyFrom(&rhs->armorAddons);
			lhs->templateArmor = rhs->templateArmor;
		}
		break;

		default:
			// unsupported
			break;
	}
}
]]
        ""
        "LE-SKSE-017")

    set(_skyrim_le_game_rtti "${_skyrim_le_sdk_source_root}/src/skse/skse/GameRTTI.h")
    _skyrim_le_replace_exact(
        "${_skyrim_le_game_rtti}"
        "#define DYNAMIC_CAST(obj, from, to) ( ## to *) Runtime_DynamicCast((void*)(obj), 0, RTTI_ ## from, RTTI_ ## to, 0)"
        "#define DYNAMIC_CAST(obj, from, to) ((to*)Runtime_DynamicCast((void*)(obj), 0, RTTI_ ## from, RTTI_ ## to, 0))"
        "LE-SKSE-011")

    set(_skyrim_le_utilities "${_skyrim_le_sdk_source_root}/src/skse/skse/Utilities.h")
    _skyrim_le_replace_exact(
        "${_skyrim_le_utilities}"
        "#pragma once"
        "#pragma once\n\n#include <string>"
        "LE-SKSE-012")

    set(_skyrim_le_game_menus "${_skyrim_le_sdk_source_root}/src/skse/skse/GameMenus.cpp")
    _skyrim_le_replace_exact(
        "${_skyrim_le_sdk_source_root}/src/skse/skse/GameMenus.h"
        "#pragma once"
        "#pragma once\n\n#include \"skse/ScaleformMovie.h\""
        "LE-SKSE-013")

    set(_skyrim_le_papyrus_events "${_skyrim_le_sdk_source_root}/src/skse/skse/PapyrusEvents.h")
    _skyrim_le_replace_exact(
        "${_skyrim_le_papyrus_events}"
        "Lock()"
        "this->Lock()"
        "LE-SKSE-014a"
        18)
    _skyrim_le_replace_exact(
        "${_skyrim_le_papyrus_events}"
        "Release()"
        "this->Release()"
        "LE-SKSE-014b"
        18)
    _skyrim_le_replace_exact(
        "${_skyrim_le_papyrus_events}"
        "m_data"
        "this->m_data"
        "LE-SKSE-014c"
        25)
    _skyrim_le_replace_exact(
        "${_skyrim_le_papyrus_events}"
        "RegMap::iterator"
        "typename RegMap::iterator"
        "LE-SKSE-014d"
        4)
    _skyrim_le_replace_exact(
        "${_skyrim_le_papyrus_events}"
        "RegSet::iterator"
        "typename RegSet::iterator"
        "LE-SKSE-014e"
        4)
endfunction()
