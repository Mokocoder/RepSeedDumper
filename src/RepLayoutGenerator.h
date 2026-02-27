#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Unreal/ObjectArray.h"
#include "PredefinedMembers.h"

namespace fs = std::filesystem;

struct RepHandle;

class RepLayoutGenerator
{
public:
	static inline PredefinedMemberLookupMapType PredefinedMembers;

	static inline std::string MainFolderName = "RepLayout";
	static inline std::string SubfolderName = "";

	static inline fs::path MainFolder;
	static inline fs::path Subfolder;

public:
	static void Generate();

	static void InitPredefinedMembers() { }
	static void InitPredefinedFunctions() { }

	static bool HasNetSerializer(UEStruct ScriptStruct);
	static std::string GetRepTypeString(UEProperty Prop);
	static int32 GetEnumMax(UEEnum Enum);

private:
	static void ExpandProperty(UEProperty Prop, const std::string& Prefix, const std::string& OwnerClassName, int32& HandleCounter, std::vector<RepHandle>& OutHandles);
};
