#pragma once

#include "Core/CoreTypes.h"

// ====================================================================
// TSparseArray (Simplified version for KraftonEngine)
// 
// 연속된 인덱스를 반환하지만, 중간에 요소가 지워져도(Remove) O(1)에 처리하고
// 삭제된 빈 슬롯(인덱스)을 재활용(FreeList)하여 효율적으로 데이터를 관리하는 
// 언리얼 엔진 스타일 Sparse Array 컨테이너.
// ====================================================================
template <typename T>
class TSparseArray
{
public:
	// 배열 요소의 내부 래퍼 구조체.
	// bIsValid가 참이면 데이터가 유효하며, 거짓이면 FreeList 연접(Linked List)용 NextFreeIndex를 저장한다.
	struct FElementData
	{
		T Value{};
		int32 NextFreeIndex = -1; // bIsValid == false일 때, 다음 빈 슬롯의 인덱스를 가리킴
		bool bIsValid = false;
	};

	TSparseArray() = default;
	~TSparseArray() = default;

	// 배열 내의 새 원소를 추가하고 할당된 인덱스(고유 ID 역할)를 반환한다.
	int32 Add(const T& InValue)
	{
		int32 AllocatedIndex = -1;

		if (FirstFreeIndex != -1)
		{
			// 재활용할 빈 슬롯이 있으면 거기부터 채운다.
			AllocatedIndex = FirstFreeIndex;
			FirstFreeIndex = Data[AllocatedIndex].NextFreeIndex;
		}
		else
		{
			// 빈 슬롯이 없으면 배열 뒤에 새로 추가한다.
			AllocatedIndex = static_cast<int32>(Data.size());
			Data.push_back(FElementData());
		}

		// 데이터 복사 및 유효성 갱신
		Data[AllocatedIndex].Value = InValue;
		Data[AllocatedIndex].bIsValid = true;
		ActiveCount++;

		return AllocatedIndex;
	}

	// 주어진 인덱스의 요소를 O(1)으로 삭제하고, 해당 슬롯을 FreeList에 넣는다.
	void Remove(int32 Index)
	{
		if (IsValidIndex(Index))
		{
			Data[Index].bIsValid = false;
			Data[Index].NextFreeIndex = FirstFreeIndex; // 현재의 FirstFreeIndex를 다음으로 밀고
			FirstFreeIndex = Index;                     // 내가 새로운 FirstFreeIndex가 됨
			ActiveCount--;
		}
	}

	// 주어진 인덱스가 현재 유효한(삭제되지 않은) 데이터인지 확인한다.
	bool IsValidIndex(int32 Index) const
	{
		return Index >= 0 && Index < static_cast<int32>(Data.size()) && Data[Index].bIsValid;
	}

	// 배열 원소에 접근 (유효한 인덱스라고 가정)
	T& operator[](int32 Index)
	{
		return Data[Index].Value;
	}

	const T& operator[](int32 Index) const
	{
		return Data[Index].Value;
	}

	// 배열의 총 용량(삭제된 잉여 데이터 포함 크기)
	int32 GetMaxIndex() const
	{
		return static_cast<int32>(Data.size());
	}

	// 현재 씬에 살아있는(유효한) 총 원소의 개수
	int32 Num() const
	{
		return ActiveCount;
	}

	// --- Iterator (For-range loop 지원) ---
	// 빈 슬롯을 건너뛰고 오직 유효한 요소만 순회할 수 있도록 돕는 반복자
	class FIterator
	{
	public:
		FIterator(TSparseArray& InArray, int32 InIndex)
			: Array(InArray), CurrentIndex(InIndex)
		{
			AdvanceToValid();
		}

		T& operator*() { return Array[CurrentIndex]; }
		T* operator->() { return &Array[CurrentIndex]; }

		FIterator& operator++()
		{
			CurrentIndex++;
			AdvanceToValid();
			return *this;
		}

		bool operator!=(const FIterator& Other) const
		{
			return CurrentIndex != Other.CurrentIndex;
		}

	private:
		void AdvanceToValid()
		{
			while (CurrentIndex < Array.GetMaxIndex() && !Array.IsValidIndex(CurrentIndex))
			{
				CurrentIndex++;
			}
		}

		TSparseArray& Array;
		int32 CurrentIndex;
	};

	class FConstIterator
	{
	public:
		FConstIterator(const TSparseArray& InArray, int32 InIndex)
			: Array(InArray), CurrentIndex(InIndex)
		{
			AdvanceToValid();
		}

		const T& operator*() const { return Array[CurrentIndex]; }
		const T* operator->() const { return &Array[CurrentIndex]; }

		FConstIterator& operator++()
		{
			CurrentIndex++;
			AdvanceToValid();
			return *this;
		}

		bool operator!=(const FConstIterator& Other) const
		{
			return CurrentIndex != Other.CurrentIndex;
		}

	private:
		void AdvanceToValid()
		{
			while (CurrentIndex < Array.GetMaxIndex() && !Array.IsValidIndex(CurrentIndex))
			{
				CurrentIndex++;
			}
		}

		const TSparseArray& Array;
		int32 CurrentIndex;
	};

	FIterator begin() { return FIterator(*this, 0); }
	FIterator end() { return FIterator(*this, GetMaxIndex()); }
	FConstIterator begin() const { return FConstIterator(*this, 0); }
	FConstIterator end() const { return FConstIterator(*this, GetMaxIndex()); }

private:
	TArray<FElementData> Data;
	int32 FirstFreeIndex = -1; // 연결 리스트 형태로 빈 슬롯들을 추적하는 헤드 (head of freelist)
	int32 ActiveCount = 0;     // 현재 살아있는 원소 수
};
