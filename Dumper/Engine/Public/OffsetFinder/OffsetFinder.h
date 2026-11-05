#pragma once

#include <vector>

#include "Unreal/ObjectArray.h"

#include "Platform.h"

namespace OffsetFinder
{
	constexpr int32 OffsetNotFound = -1;
	constexpr int32 OffsetFinderMinValue = Platform::Is32Bit() ? 0x18 : 0x28;
	constexpr int32 OffsetFinderMaxValue = 0x1A0;

	template<int Alignement = 4, typename T>
	inline int32_t FindOffset(const std::vector<std::pair<void*, T>>& ObjectValuePair, int MinOffset = OffsetFinderMinValue, int MaxOffset = OffsetFinderMaxValue)
	{
		static_assert(Alignement > 0);
		bool bHasObject = false;
		for (size_t i = 0; i < ObjectValuePair.size(); i++)
		{
			if (ObjectValuePair[i].first == nullptr)
			{
				std::cerr << "Dumper-7 ERROR: FindOffset is skipping ObjectValuePair[" << i << "] because .first is nullptr." << std::endl;
			}
			else
			{
				bHasObject = true;
			}
		}

		if (!bHasObject || MinOffset < 0)
			return OffsetNotFound;

		// An offset is useful only when every available sample agrees at that
		// same position. Restarting a sample loop after increasing an offset can
		// skip the first sample and retain a match from a different position.
		for (int Offset = MinOffset; Offset < MaxOffset; Offset += Alignement)
		{
			bool bAllMatch = true;
			for (const auto& [Object, ExpectedValue] : ObjectValuePair)
			{
				if (Object != nullptr && *reinterpret_cast<const T*>(static_cast<const uint8_t*>(Object) + Offset) != ExpectedValue)
				{
					bAllMatch = false;
					break;
				}
			}
			if (bAllMatch)
				return Offset;
		}

		return OffsetNotFound;
	}

	template<bool bCheckForVft = true>
	inline int32_t GetValidPointerOffset(const void* PtrObjA, const void* PtrObjB, int32_t StartingOffset, int32_t MaxOffset, bool bNeedsToBeInProcessMemory = false)
	{
		const uint8_t* ObjA = static_cast<const uint8_t*>(PtrObjA);
		const uint8_t* ObjB = static_cast<const uint8_t*>(PtrObjB);

		if (Platform::IsBadReadPtr(ObjA) || Platform::IsBadReadPtr(ObjB))
			return OffsetNotFound;

		for (int j = StartingOffset; j <= MaxOffset; j += sizeof(void*))
		{
			const bool bIsAValid = !Platform::IsBadReadPtr(*reinterpret_cast<void* const*>(ObjA + j)) && (bCheckForVft ? !Platform::IsBadReadPtr(**reinterpret_cast<void** const*>(ObjA + j)) : true);
			const bool bIsBValid = !Platform::IsBadReadPtr(*reinterpret_cast<void* const*>(ObjB + j)) && (bCheckForVft ? !Platform::IsBadReadPtr(**reinterpret_cast<void** const*>(ObjB + j)) : true);

			if (bNeedsToBeInProcessMemory)
			{
				if (!Platform::IsAddressInProcessRange(*reinterpret_cast<void* const*>(ObjA + j)) || !Platform::IsAddressInProcessRange(*reinterpret_cast<void* const*>(ObjB + j)))
					continue;
			}

			if (bIsAValid && bIsBValid)
				return j;
		}

		return OffsetNotFound;
	};

	struct FFieldLayoutSample
	{
		const void* FirstField;
		const void* Owner;
		int32_t FieldCount;
	};

	struct FFieldOwnerAndNextOffsets
	{
		int32_t Owner = OffsetNotFound;
		int32_t Next = OffsetNotFound;
		bool bTaggedOwner = false;
	};

	inline FFieldOwnerAndNextOffsets FindFFieldOwnerAndNextOffsets(const std::vector<FFieldLayoutSample>& Samples, int32_t ClassOffset, int32_t MaxOffset = 0x48)
	{
		if (Samples.size() < 2)
			return {};

		for (int32_t OwnerOffset = sizeof(void*); OwnerOffset <= MaxOffset; OwnerOffset += sizeof(void*))
		{
			if (OwnerOffset == ClassOffset)
				continue;
			bool bOwnerMatches = true;
			int OwnerTag = -1;
			for (const auto& Sample : Samples)
			{
				const auto* Field = static_cast<const uint8_t*>(Sample.FirstField);
				if (!Field || !Sample.Owner || Sample.FieldCount < 2 || Sample.FieldCount > 64 || Platform::IsBadReadPtr(Field) || Platform::IsBadReadPtr(Field + MaxOffset + sizeof(void*) - 1))
				{
					bOwnerMatches = false;
					break;
				}
				const uintptr_t RawOwner = *reinterpret_cast<const uintptr_t*>(Field + OwnerOffset);
				if ((RawOwner & ~uintptr_t(1)) != reinterpret_cast<uintptr_t>(Sample.Owner) || (OwnerTag != -1 && OwnerTag != (RawOwner & 1)))
				{
					bOwnerMatches = false;
					break;
				}
				OwnerTag = static_cast<int>(RawOwner & 1);
			}
			if (!bOwnerMatches)
				continue;

			for (int32_t NextOffset = sizeof(void*); NextOffset <= MaxOffset; NextOffset += sizeof(void*))
			{
				if (NextOffset == ClassOffset || NextOffset == OwnerOffset)
					continue;
				bool bChainsMatch = true;
				for (const auto& Sample : Samples)
				{
					const auto* Field = static_cast<const uint8_t*>(Sample.FirstField);
					std::vector<const uint8_t*> Seen;
					for (int32_t Index = 0; Index < Sample.FieldCount; ++Index)
					{
						bool bRepeated = false;
						for (const auto* Previous : Seen)
							bRepeated |= Previous == Field;
						if (!Field || bRepeated || (reinterpret_cast<uintptr_t>(Field) % alignof(void*)) != 0 || Platform::IsBadReadPtr(Field) || Platform::IsBadReadPtr(Field + MaxOffset + sizeof(void*) - 1))
						{
							bChainsMatch = false;
							break;
						}
						Seen.push_back(Field);
						const uintptr_t RawOwner = *reinterpret_cast<const uintptr_t*>(Field + OwnerOffset);
						if ((RawOwner & ~uintptr_t(1)) != reinterpret_cast<uintptr_t>(Sample.Owner) || static_cast<int>(RawOwner & 1) != OwnerTag)
						{
							bChainsMatch = false;
							break;
						}
						Field = *reinterpret_cast<const uint8_t* const*>(Field + NextOffset);
					}
					if (!bChainsMatch || Field != nullptr)
					{
						bChainsMatch = false;
						break;
					}
				}
				if (bChainsMatch)
					return { OwnerOffset, NextOffset, OwnerTag == 1 };
			}
		}
		return {};
	}

	// Container metadata must point to an FProperty owned by that container,
	// rather than merely to readable memory (which can also be an ArrayFlags value).
	// Readers are supplied so the decision can be checked with synthetic layouts.
	template<typename ReadPointer, typename IsProperty>
	inline int32_t FindContainerPropertyPointerOffset(const std::vector<const void*>& Samples, int32_t PropertySize, int32_t OwnerOffset, int32_t PointerCount, ReadPointer&& Read, IsProperty&& IsPropertyField)
	{
		if (Samples.size() < 2 || PropertySize < 0 || OwnerOffset < 0 || PointerCount < 1 || PointerCount > 2)
			return OffsetNotFound;
		for (size_t i = 0; i < Samples.size(); ++i)
		{
			if (!Samples[i])
				return OffsetNotFound;
			for (size_t j = 0; j < i; ++j)
				if (Samples[i] == Samples[j])
					return OffsetNotFound;
		}

		for (int32_t Offset = PropertySize; Offset <= PropertySize + 0x20; Offset += sizeof(void*))
		{
			bool bAllMatch = true;
			for (const void* Sample : Samples)
			{
				for (int32_t Index = 0; Index < PointerCount; ++Index)
				{
					uintptr_t Inner = 0, Owner = 0;
					if (!Read(reinterpret_cast<uintptr_t>(Sample) + Offset + Index * sizeof(void*), Inner)
						|| !Inner || (Inner % alignof(void*)) != 0 || !IsPropertyField(Inner)
						|| !Read(Inner + OwnerOffset, Owner) || Owner != reinterpret_cast<uintptr_t>(Sample))
					{
						bAllMatch = false;
						break;
					}
				}
				if (!bAllMatch)
					break;
			}
			if (bAllMatch)
				return Offset;
		}
		return OffsetNotFound;
	}

	/* UObject */
	int32_t FindUObjectFlagsOffset();
	int32_t FindUObjectIndexOffset();
	int32_t FindUObjectClassOffset();
	int32_t FindUObjectNameOffset();
	int32_t FindUObjectOuterOffset();

	void FixupHardcodedOffsets();
	void InitFNameSettings();
	void PostInitFNameSettings();

	/* UField */
	int32_t FindUFieldNextOffset();

	/* FField */
	bool InitFFieldOwnerAndNextOffsets();
	int32_t FindFFieldNextOffset();
	int32_t FindFFieldNameOffset();
	int32_t NewFindFFieldNameOffset();
	int32_t FindFFieldClassOffset();
	int32_t FindFFieldEditorOnlyMetaDataOffset();

	/* FFieldClass */
	int32_t FindFieldClassNameOffset();
	int32_t FindFieldClassIdOffset();
	int32_t FindFieldClassCastFlagsOffset();
	void FixupFieldClassOffsets();

	/* UEnum */
	int32_t FindEnumNamesOffset();
	int32_t FindEnumUnderlayingTypeOffset();

	/* UStruct */
	int32_t FindSuperOffset();
	int32_t FindChildOffset();
	int32_t FindChildPropertiesOffset();
	int32_t FindStructSizeOffset();
	int32_t FindMinAlignmentOffset();
	int32_t FindStructBaseChainOffset();

	/* UFunction */
	int32_t FindFunctionFlagsOffset();
	int32_t FindFunctionNativeFuncOffset();

	/* UClass */
	int32_t FindCastFlagsOffset();
	int32_t FindDefaultObjectOffset();
	int32_t FindImplementedInterfacesOffset();

	/* Property */
	int32_t FindElementSizeOffset();
	int32_t FindArrayDimOffset();
	int32_t FindPropertyFlagsOffset();
	int32_t FindOffsetInternalOffset();

	/* BoolProperty */
	int32_t FindBoolPropertyBaseOffset();

	/* ObjectProperty */
	int32_t FindObjectPropertyClassOffset();

	/* EnumProperty */
	int32_t FindEnumPropertyBaseOffset();
	
	/* ByteProperty */
	int32_t FindBytePropertyEnumOffset();

	/* StructProperty */
	int32_t FindStructPropertyStructOffset();

	/* DelegateProperty */
	int32_t FindDelegatePropertySignatureFunctionOffset();

	/* ArrayProperty */
	int32_t FindInnerTypeOffset(const int32 PropertySize);

	/* SetProperty */
	int32_t FindSetPropertyBaseOffset(const int32 PropertySize);

	/* MapProperty */
	int32_t FindMapPropertyBaseOffset(const int32 PropertySize);

	/* InSDK -> ULevel */
	int32_t FindLevelActorsOffset();

	/* InSDK -> UDataTable */
	int32_t FindDatatableRowMapOffset();
}
