#pragma once

#include <format>
#include <vector>
#include <iostream>

#include "OffsetFinder/Offsets.h"
#include "Unreal/ObjectArray.h"

/* Off::UScriptStruct::StructFlags - not present in upstream Offsets.h */
namespace Off { namespace UScriptStruct { inline int32 StructFlags = -1; } }

/* Must be called after Off::Init() */
inline void InitStructFlagsOffset()
{
	constexpr uint32 STRUCT_NetSerializeNative = 0x00000400;

	struct FlagProbe { const char* Name; bool bExpectNetSerialize; };
	FlagProbe Probes[] = {
		{ "Vector",      true  },
		{ "Rotator",     true  },
		{ "Guid",        false },
		{ "LinearColor", false },
	};

	struct Sample { const uint8* Ptr; bool bExpect; };
	std::vector<Sample> Samples;

	for (auto& [Name, bExpect] : Probes)
	{
		UEObject Obj = ObjectArray::FindObjectFast<UEObject>(Name, EClassCastFlags::ScriptStruct);
		if (!Obj)
			continue;

		Samples.push_back({ reinterpret_cast<const uint8*>(Obj.GetAddress()), bExpect });
	}

	bool bHasPos = false, bHasNeg = false;
	for (auto& S : Samples) { if (S.bExpect) bHasPos = true; else bHasNeg = true; }

	if (bHasPos && bHasNeg)
	{
		const int32 ScanStart = (Off::UStruct::MinAlignment + sizeof(int32) + 3) & ~3;
		const int32 ScanEnd = ScanStart + 0x80;

		for (int32 i = ScanStart; i < ScanEnd; i += sizeof(int32))
		{
			bool bAllMatch = true;

			for (auto& S : Samples)
			{
				uint32 Flags = *reinterpret_cast<const uint32*>(S.Ptr + i);
				bool bHasBit = (Flags & STRUCT_NetSerializeNative) != 0;

				if (bHasBit != S.bExpect)
				{
					bAllMatch = false;
					break;
				}
			}

			if (bAllMatch)
			{
				Off::UScriptStruct::StructFlags = i;
				std::cerr << std::format("Off::UScriptStruct::StructFlags: 0x{:X}\n", i);
				break;
			}
		}
	}

	if (Off::UScriptStruct::StructFlags == -1)
		std::cerr << "UScriptStruct::StructFlags could not be determined (non-critical)\n";
}
