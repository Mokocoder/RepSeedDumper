#include "RepLayoutGenerator.h"
#include "StructFlagsOffset.h"
#include "Platform.h"

#include "Json/json.hpp"

#include <fstream>
#include <chrono>
#include <format>
#include <unordered_set>


static std::unordered_set<const void*> NetSerializerCache;
static std::unordered_set<const void*> NotNetSerializerCache;

bool RepLayoutGenerator::HasNetSerializer(UEStruct ScriptStruct)
{
	constexpr uint32 STRUCT_NetSerializeNative = 0x00000400;

	if (Off::UScriptStruct::StructFlags == -1)
		return false;

	const void* Addr = ScriptStruct.GetAddress();

	if (NetSerializerCache.count(Addr))
		return true;
	if (NotNetSerializerCache.count(Addr))
		return false;

	const uint8* StructPtr = reinterpret_cast<const uint8*>(Addr);
	uint32 Flags = *reinterpret_cast<const uint32*>(StructPtr + Off::UScriptStruct::StructFlags);
	bool bResult = (Flags & STRUCT_NetSerializeNative) != 0;

	if (bResult)
		NetSerializerCache.insert(Addr);
	else
		NotNetSerializerCache.insert(Addr);

	return bResult;
}

std::string RepLayoutGenerator::GetRepTypeString(UEProperty Prop)
{
	EClassCastFlags CastFlags = Prop.GetCastFlags();

	if (CastFlags & EClassCastFlags::BoolProperty)
		return "bool";
	if (CastFlags & EClassCastFlags::ByteProperty)
	{
		UEByteProperty ByteProp = Prop.Cast<UEByteProperty>();
		UEEnum Enum = ByteProp.GetEnum();
		if (Enum)
			return "byte";
		return "byte";
	}
	if (CastFlags & EClassCastFlags::Int8Property)
		return "int8";
	if (CastFlags & EClassCastFlags::Int16Property)
		return "int16";
	if (CastFlags & EClassCastFlags::IntProperty)
		return "int32";
	if (CastFlags & EClassCastFlags::Int64Property)
		return "int64";
	if (CastFlags & EClassCastFlags::UInt16Property)
		return "uint16";
	if (CastFlags & EClassCastFlags::UInt32Property)
		return "uint32";
	if (CastFlags & EClassCastFlags::UInt64Property)
		return "uint64";
	if (CastFlags & EClassCastFlags::FloatProperty)
		return "float";
	if (CastFlags & EClassCastFlags::DoubleProperty)
		return "double";
	if (CastFlags & EClassCastFlags::StrProperty)
		return "string";
	if (CastFlags & EClassCastFlags::NameProperty)
		return "name";
	if (CastFlags & EClassCastFlags::TextProperty)
		return "text";
	if (CastFlags & EClassCastFlags::WeakObjectProperty)
		return "weak_obj";
	if (CastFlags & EClassCastFlags::LazyObjectProperty)
		return "lazy_obj";
	if (CastFlags & EClassCastFlags::SoftClassProperty)
		return "soft_obj";
	if (CastFlags & EClassCastFlags::SoftObjectProperty)
		return "soft_obj";
	if (CastFlags & EClassCastFlags::ClassProperty)
		return "class_ref";
	if (CastFlags & EClassCastFlags::InterfaceProperty)
		return "interface";
	if (CastFlags & EClassCastFlags::ObjectProperty || CastFlags & EClassCastFlags::ObjectPropertyBase)
		return "object";
	if (CastFlags & EClassCastFlags::StructProperty)
	{
		UEStructProperty StructProp = Prop.Cast<UEStructProperty>();
		UEStruct Inner = StructProp.GetUnderlayingStruct();
		if (Inner)
			return "struct:" + Inner.GetName();
		return "struct:Unknown";
	}
	if (CastFlags & EClassCastFlags::ArrayProperty)
		return "array";
	if (CastFlags & EClassCastFlags::MapProperty)
		return "map";
	if (CastFlags & EClassCastFlags::SetProperty)
		return "set";
	if (CastFlags & EClassCastFlags::EnumProperty)
		return "enum";
	if (CastFlags & EClassCastFlags::DelegateProperty)
		return "delegate";
	if (CastFlags & EClassCastFlags::MulticastDelegateProperty ||
		CastFlags & EClassCastFlags::MulticastInlineDelegateProperty ||
		CastFlags & EClassCastFlags::MulticastSparseDelegateProperty)
		return "multicast_delegate";

	return "unknown";
}

int32 RepLayoutGenerator::GetEnumMax(UEEnum Enum)
{
	if (!Enum)
		return -1;

	auto Pairs = Enum.GetNameValuePairs();
	int64 MaxVal = 0;

	for (auto& [Name, Value] : Pairs)
	{
		/* UE's GetMaxEnumValue() includes _MAX sentinel; SerializeInt uses (MaxValue + 1) */
		if (Value + 1 > MaxVal)
			MaxVal = Value + 1;
	}

	return static_cast<int32>(MaxVal);
}

static nlohmann::json GetEnumValues(UEEnum Enum)
{
	nlohmann::json Values = nlohmann::json::object();
	if (!Enum)
		return Values;

	for (auto& [Name, Value] : Enum.GetNameValuePairs())
	{
		std::string Short = Name.ToString();
		auto Pos = Short.rfind("::");
		if (Pos != std::string::npos)
			Short = Short.substr(Pos + 2);

		if (Short == "MAX" || Short.ends_with("_MAX"))
			continue;

		Values[Short] = Value;
	}
	return Values;
}

static nlohmann::json ExpandStructFields(UEStruct Inner, int Depth = 0);

static void AnnotateProperty(nlohmann::json& Entry, UEProperty Prop, int Depth = 0)
{
	EClassCastFlags Cast = Prop.GetCastFlags();

	if (Cast & EClassCastFlags::StructProperty)
	{
		UEStructProperty SP = Prop.Cast<UEStructProperty>();
		UEStruct St = SP.GetUnderlayingStruct();
		if (St)
		{
			Entry["struct_type"] = St.GetName();
			if (Depth < 4)
			{
				nlohmann::json Fields = ExpandStructFields(St, Depth + 1);
				if (!Fields.empty())
					Entry["fields"] = std::move(Fields);
			}
		}
	}
	else if (Cast & EClassCastFlags::ArrayProperty)
	{
		UEArrayProperty AP = Prop.Cast<UEArrayProperty>();
		UEProperty InnerP = AP.GetInnerProperty();
		if (InnerP)
		{
			Entry["inner_type"] = RepLayoutGenerator::GetRepTypeString(InnerP);
			nlohmann::json InnerDetail = nlohmann::json::object();
			AnnotateProperty(InnerDetail, InnerP, Depth);
			if (InnerDetail.size() > 0)
				Entry.update(InnerDetail);
		}
	}
	else if (Cast & EClassCastFlags::ByteProperty)
	{
		UEByteProperty BP = Prop.Cast<UEByteProperty>();
		UEEnum E = BP.GetEnum();
		if (E)
		{
			Entry["enum"] = E.GetName();
			int32 Max = RepLayoutGenerator::GetEnumMax(E);
			if (Max > 0) Entry["max"] = Max;
			Entry["values"] = GetEnumValues(E);
		}
	}
	else if (Cast & EClassCastFlags::EnumProperty)
	{
		UEEnumProperty EP = Prop.Cast<UEEnumProperty>();
		UEEnum E = EP.GetEnum();
		if (E)
		{
			Entry["enum"] = E.GetName();
			int32 Max = RepLayoutGenerator::GetEnumMax(E);
			if (Max > 0) Entry["max"] = Max;
			Entry["values"] = GetEnumValues(E);
		}
	}
	else if (Cast & EClassCastFlags::ClassProperty)
	{
		UEClassProperty CP = Prop.Cast<UEClassProperty>();
		UEClass Meta = CP.GetMetaClass();
		if (Meta)
			Entry["meta_class"] = Meta.GetName();
	}
	else if (Cast & EClassCastFlags::ObjectProperty || Cast & EClassCastFlags::ObjectPropertyBase)
	{
		UEObjectProperty OP = Prop.Cast<UEObjectProperty>();
		UEClass PC = OP.GetPropertyClass();
		if (PC)
			Entry["object_class"] = PC.GetName();
	}
}

static nlohmann::json ExpandStructFields(UEStruct Inner, int Depth)
{
	nlohmann::json FieldsArr = nlohmann::json::array();

	std::vector<UEStruct> Chain;
	{
		UEStruct Cur = Inner;
		while (Cur) { Chain.push_back(Cur); Cur = Cur.GetSuper(); }
		std::reverse(Chain.begin(), Chain.end());
	}

	for (UEStruct& Anc : Chain)
	{
		for (UEProperty Field : Anc.GetProperties())
		{
			nlohmann::json E;
			E["name"] = Field.GetName();
			E["type"] = RepLayoutGenerator::GetRepTypeString(Field);
			E["offset"] = Field.GetOffset();
			E["size"] = Field.GetSize();
			AnnotateProperty(E, Field, Depth);
			FieldsArr.push_back(std::move(E));
		}
	}

	return FieldsArr;
}


struct RepHandle
{
	int32 Handle;
	std::string Name;
	std::string Type;
	std::string OwnerClass;
	std::string EnumName;
	int32 EnumMax = -1;
	std::vector<RepHandle> InnerHandles;
};

void RepLayoutGenerator::ExpandProperty(
	UEProperty Prop,
	const std::string& Prefix,
	const std::string& OwnerClassName,
	int32& HandleCounter,
	std::vector<RepHandle>& OutHandles)
{
	EClassCastFlags CastFlags = Prop.GetCastFlags();
	int32 ArrayDim = Prop.GetArrayDim();
	std::string PropName = Prop.GetName();

	std::string FullName = Prefix.empty() ? PropName : (Prefix + "." + PropName);

	/* ArrayProperty */
	if (CastFlags & EClassCastFlags::ArrayProperty)
	{
		RepHandle Handle;
		Handle.Handle = HandleCounter++;
		Handle.Name = FullName;
		Handle.Type = "array";
		Handle.OwnerClass = OwnerClassName;

		UEArrayProperty ArrProp = Prop.Cast<UEArrayProperty>();
		UEProperty InnerProp = ArrProp.GetInnerProperty();
		if (InnerProp)
		{
			int32 InnerCounter = 1;
			ExpandProperty(InnerProp, "", "", InnerCounter, Handle.InnerHandles);
		}

		OutHandles.push_back(std::move(Handle));
		return;
	}

	/* Map, Set, Delegate: single handle, no inner expansion */
	if (CastFlags & EClassCastFlags::MapProperty ||
		CastFlags & EClassCastFlags::SetProperty ||
		CastFlags & EClassCastFlags::DelegateProperty ||
		CastFlags & EClassCastFlags::MulticastDelegateProperty ||
		CastFlags & EClassCastFlags::MulticastInlineDelegateProperty ||
		CastFlags & EClassCastFlags::MulticastSparseDelegateProperty)
	{
		RepHandle Handle;
		Handle.Handle = HandleCounter++;
		Handle.Name = FullName;
		Handle.Type = GetRepTypeString(Prop);
		Handle.OwnerClass = OwnerClassName;
		OutHandles.push_back(std::move(Handle));
		return;
	}

	/* StructProperty */
	if (CastFlags & EClassCastFlags::StructProperty)
	{
		UEStructProperty StructProp = Prop.Cast<UEStructProperty>();
		UEStruct InnerStruct = StructProp.GetUnderlayingStruct();

		bool bHasNetSerializer = InnerStruct && HasNetSerializer(InnerStruct);

		if (bHasNetSerializer)
		{
			/* NetSerializer structs are opaque on the wire */
			for (int32 i = 0; i < ArrayDim; i++)
			{
				RepHandle Handle;
				Handle.Handle = HandleCounter++;
				Handle.Name = (ArrayDim > 1) ? (FullName + "[" + std::to_string(i) + "]") : FullName;
				Handle.Type = "struct:" + InnerStruct.GetName();
				Handle.OwnerClass = OwnerClassName;
				OutHandles.push_back(std::move(Handle));
			}
			return;
		}

		/* No NetSerializer: flatten sub-fields */
		if (InnerStruct)
		{
			/* Walk Super chain (root first) to collect all properties */
			std::vector<UEProperty> AllSubProps;
			{
				std::vector<UEStruct> StructChain;
				UEStruct Current = InnerStruct;
				while (Current)
				{
					StructChain.push_back(Current);
					Current = Current.GetSuper();
				}
				std::reverse(StructChain.begin(), StructChain.end());

				for (UEStruct& Ancestor : StructChain)
				{
					for (UEProperty SubProp : Ancestor.GetProperties())
						AllSubProps.push_back(SubProp);
				}
			}

			for (int32 i = 0; i < ArrayDim; i++)
			{
				std::string ArrayPrefix = (ArrayDim > 1) ? (FullName + "[" + std::to_string(i) + "]") : FullName;

				for (UEProperty SubProp : AllSubProps)
				{
					EPropertyFlags SubPropFlags = SubProp.GetPropertyFlags();
					if (SubPropFlags & EPropertyFlags::RepSkip)
						continue;

					ExpandProperty(SubProp, ArrayPrefix, OwnerClassName, HandleCounter, OutHandles);
				}
			}
			return;
		}
	}

	/* Scalar property */
	for (int32 i = 0; i < ArrayDim; i++)
	{
		RepHandle Handle;
		Handle.Handle = HandleCounter++;
		Handle.Name = (ArrayDim > 1) ? (FullName + "[" + std::to_string(i) + "]") : FullName;
		Handle.Type = GetRepTypeString(Prop);
		Handle.OwnerClass = OwnerClassName;

		/* Resolve enum info */
		if (CastFlags & EClassCastFlags::ByteProperty)
		{
			UEByteProperty ByteProp = Prop.Cast<UEByteProperty>();
			UEEnum Enum = ByteProp.GetEnum();
			if (Enum)
			{
				Handle.EnumName = Enum.GetName();
				Handle.EnumMax = GetEnumMax(Enum);
			}
		}
		else if (CastFlags & EClassCastFlags::EnumProperty)
		{
			UEEnumProperty EnumProp = Prop.Cast<UEEnumProperty>();
			UEEnum Enum = EnumProp.GetEnum();
			if (Enum)
			{
				Handle.EnumName = Enum.GetName();
				Handle.EnumMax = GetEnumMax(Enum);
			}
		}

		OutHandles.push_back(std::move(Handle));
	}
}


static nlohmann::json SerializeHandle(const RepHandle& H, bool bIncludeClass)
{
	nlohmann::json Entry;
	Entry["h"] = H.Handle;
	Entry["name"] = H.Name;
	Entry["type"] = H.Type;

	if (bIncludeClass && !H.OwnerClass.empty())
		Entry["class"] = H.OwnerClass;

	if (!H.EnumName.empty())
	{
		Entry["enum"] = H.EnumName;
		if (H.EnumMax > 0)
			Entry["max"] = H.EnumMax;
	}

	if (!H.InnerHandles.empty())
	{
		nlohmann::json InnerArray = nlohmann::json::array();
		for (auto& IH : H.InnerHandles)
			InnerArray.push_back(SerializeHandle(IH, false));
		Entry["inner"] = std::move(InnerArray);
	}

	return Entry;
}


void RepLayoutGenerator::Generate()
{
	auto StartTime = std::chrono::high_resolution_clock::now();

	std::cerr << "Generating replication data...\n";

	nlohmann::json Root;
	nlohmann::json& Classes = Root["classes"];

	int32 TotalClasses = 0;
	int32 StructTypesResolved = 0;
	for (UEObject Obj : ObjectArray())
	{
		if (!Obj.IsA(EClassCastFlags::Class))
			continue;

		/* Skip Blueprint-generated classes; only native C++ classes belong in the seed */
		std::string MetaClassName = Obj.GetClass().GetName();
		if (MetaClassName != "Class")
			continue;

		UEClass Class = Obj.Cast<UEClass>();
		std::string ClassName = Class.GetName();

		/* Build inheritance chain (root -> leaf) */
		std::vector<UEClass> InheritanceChain;
		{
			UEStruct Current = Class;
			while (Current)
			{
				if (Current.IsA(EClassCastFlags::Class))
					InheritanceChain.push_back(Current.Cast<UEClass>());
				Current = Current.GetSuper();
			}
			std::reverse(InheritanceChain.begin(), InheritanceChain.end());
		}

		nlohmann::json ClassEntry;

		/* RepLayout handles */
		int32 HandleCounter = 1; /* 1-based */
		std::vector<RepHandle> Handles;

		for (UEClass& AncestorClass : InheritanceChain)
		{
			std::string AncestorName = AncestorClass.GetName();

			for (UEProperty Prop : AncestorClass.GetProperties())
			{
				EPropertyFlags PropFlags = Prop.GetPropertyFlags();
				if (!(PropFlags & EPropertyFlags::Net))
					continue;
				if (PropFlags & EPropertyFlags::RepSkip)
					continue;

				ExpandProperty(Prop, "", AncestorName, HandleCounter, Handles);
			}
		}

		TotalClasses++;

		if (!Handles.empty())
		{
			nlohmann::json HandleArray = nlohmann::json::array();
			for (auto& H : Handles)
			{
				HandleArray.push_back(SerializeHandle(H, true));

				if (H.Type.starts_with("struct:"))
					StructTypesResolved++;
			}
			ClassEntry["handles"] = std::move(HandleArray);
		}

		/* Net fields (properties + functions) */
		nlohmann::json NetFieldsArray = nlohmann::json::array();

		for (UEClass& AncestorClass : InheritanceChain)
		{
			std::string AncName = AncestorClass.GetName();

			for (UEProperty Prop : AncestorClass.GetProperties())
			{
				EPropertyFlags PropFlags = Prop.GetPropertyFlags();
				if (!(PropFlags & EPropertyFlags::Net))
					continue;

				std::string PropName = Prop.GetName();
				int32 ArrayDim = Prop.GetArrayDim();

				if (ArrayDim <= 1)
				{
					NetFieldsArray.push_back({ {"name", PropName}, {"type", "property"}, {"class", AncName} });
				}
				else
				{
					for (int32 i = 0; i < ArrayDim; i++)
						NetFieldsArray.push_back({ {"name", PropName + "[" + std::to_string(i) + "]"}, {"type", "property"}, {"class", AncName} });
				}
			}

			/* Collect net functions and sort by name (UE ClassNetCache ordering) */
			std::vector<UEFunction> NetFuncs;
			for (UEFunction Func : AncestorClass.GetFunctions())
			{
				if (Func.HasFlags(EFunctionFlags::Net))
					NetFuncs.push_back(Func);
			}
			std::sort(NetFuncs.begin(), NetFuncs.end(), [](const UEFunction& A, const UEFunction& B)
			{
				return A.GetName() < B.GetName();
			});

			for (UEFunction& Func : NetFuncs)
			{
				nlohmann::json FuncEntry = { {"name", Func.GetName()}, {"type", "function"}, {"class", AncName} };

				nlohmann::json ParamsArray = nlohmann::json::array();
				for (UEProperty Param : Func.GetProperties())
				{
					EPropertyFlags ParamFlags = Param.GetPropertyFlags();
					if (!(ParamFlags & EPropertyFlags::Parm))
						continue;
					if (ParamFlags & EPropertyFlags::ReturnParm)
						continue;

					nlohmann::json ParamEntry;
					ParamEntry["name"] = Param.GetName();
					ParamEntry["type"] = GetRepTypeString(Param);
					AnnotateProperty(ParamEntry, Param);

					ParamsArray.push_back(std::move(ParamEntry));
				}

				if (!ParamsArray.empty())
					FuncEntry["params"] = std::move(ParamsArray);

				void* ExecFunc = Func.GetExecFunction();
				if (ExecFunc)
					FuncEntry["offset"] = std::format("0x{:X}", Platform::GetOffset(ExecFunc));

				NetFieldsArray.push_back(std::move(FuncEntry));
			}
		}

		if (!NetFieldsArray.empty())
			ClassEntry["net_fields"] = std::move(NetFieldsArray);

		if (ClassEntry.is_null())
			ClassEntry = nlohmann::json::object();

		Classes[ClassName] = std::move(ClassEntry);
	}

	auto EndTime = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> Elapsed = EndTime - StartTime;

	Root["stats"]["total_classes"] = TotalClasses;
	Root["stats"]["struct_types_resolved"] = StructTypesResolved;
	Root["stats"]["elapsed_sec"] = Elapsed.count();

	{
		std::ofstream File(MainFolder / "replication_seed.json");
		if (File.is_open())
		{
			File << Root.dump(2);
			std::cerr << std::format("Replication: {} classes written to replication_seed.json ({:.3f}s)\n", TotalClasses, Elapsed.count());
		}
		else
		{
			std::cerr << "Failed to write replication_seed.json!\n";
		}
	}
}
