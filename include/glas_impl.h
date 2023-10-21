#pragma once

#include <span>
#include <array>
#include <tuple>
#include <string>
#include <vector>
#include <ranges>
#include <cstdint>
#include <cassert>
#include <utility>
#include <iostream>
#include <algorithm>
#include <functional>
#include <string_view>
#include <type_traits>
#include <unordered_map>


#include "glas_decl.h"
#include "glas_config.h"


namespace glas
{
	constexpr uint64_t hash(std::string_view str)
	{
		std::uint64_t hash_value = 0xcbf29ce484222325ULL;
		constexpr std::uint64_t prime = 0x100000001b3ULL;
		for (const char c : str)
		{
			hash_value ^= static_cast<uint64_t>(c);
			hash_value *= prime;
		}
		return hash_value;
	}

	constexpr uint64_t hash(std::span<const uint64_t> span)
	{
		std::uint64_t hash_value = 0xcbf29ce484222325ULL;
		constexpr std::uint64_t prime = 0x100000001b3ULL;
		for (const uint64_t c : span)
		{
			hash_value ^= static_cast<const uint64_t>(c);
			hash_value *= prime;
		}
		return hash_value;
	}

	constexpr uint32_t VariableId::GetSize() const
	{
		return IsRefOrPointer() ? static_cast<uint32_t>(sizeof(void*)) : GetTypeId().GetInfo().Size * GetArraySize();
	}

	constexpr uint32_t VariableId::GetAlign() const
	{
		return IsRefOrPointer() ? static_cast<uint32_t>(alignof(void*)) : GetTypeId().GetInfo().Align;
	}

	constexpr bool MemberInfo::IsPropertySet(MemberProperties property) const
	{
		return !!(Properties & property);
	}

	constexpr bool operator==(TypeId lhs, TypeId rhs)
	{
		return lhs.GetId() == rhs.GetId();
	}

	constexpr bool operator==(FunctionId lhs, FunctionId rhs)
	{
		return lhs.GetId() == rhs.GetId();
	}

	constexpr bool operator==(const VariableId& lhs, const VariableId& rhs)
	{
		return lhs.m_Type == rhs.m_Type &&
			lhs.m_ArraySize == rhs.m_ArraySize &&
			lhs.m_PointerAmount == rhs.m_PointerAmount &&
			lhs.m_TraitFlags == rhs.m_TraitFlags;
	}

	inline std::ostream& operator<<(std::ostream& lhs, TypeId rhs)
	{
		lhs << rhs.GetId();

		return lhs;
	}

	inline std::istream& operator>>(std::istream& lhs, TypeId rhs)
	{
		uint64_t idInt{};
		lhs >> idInt;
		rhs.SetTypeId(idInt);

		return lhs;
	}

	inline std::ostream& operator<<(std::ostream& lhs, const VariableId& rhs)
	{
		lhs << rhs.m_Type << ' '
			<< rhs.m_ArraySize << ' '
			<< rhs.m_PointerAmount << ' '
			<< rhs.m_TraitFlags;

		return lhs;
	}

	inline std::istream& operator>>(std::istream& lhs, const VariableId& rhs)
	{
		lhs >> rhs.m_Type
			>> rhs.m_ArraySize
			>> rhs.m_PointerAmount
			>> rhs.m_TraitFlags;

		return lhs;
	}

	template <typename T>
	constexpr TypeId TypeId::Create()
	{
		AutoRegisterTypeOnce<T>();
		return TypeId(TypeHash<strip_type_t<T>>());
	}

	template <typename T>
	constexpr VariableId VariableId::Create()
	{
		using Type_RemovedExtents	= std::remove_all_extents_t<T>;
		using Type_RemovedRefs		= std::remove_reference_t<Type_RemovedExtents>;
		using Type_RemovedPtrs		= remove_all_pointers_t<Type_RemovedRefs>;

		using StrippedType			= std::remove_cvref_t<Type_RemovedPtrs>;
		
		AutoRegisterTypeOnce<StrippedType> TypeRegister{};

		constexpr bool IsRef		{ std::is_reference_v<T> };
		constexpr bool IsRValRef	{ std::is_rvalue_reference_v<T> };
		constexpr bool IsConst		{ std::is_const_v<Type_RemovedPtrs> };
		constexpr bool IsVolatile	{ std::is_volatile_v<Type_RemovedPtrs> };

		constexpr uint32_t PointerAmount{ CountPointers<Type_RemovedRefs>() };

		auto variable = VariableId(TypeId::Create<StrippedType>());

		if constexpr (IsConst)		variable.SetConstFlag();
		if constexpr (IsVolatile)	variable.SetVolatileFlag();
		if constexpr (IsRef)		variable.SetReferenceFlag();
		if constexpr (IsRValRef)	variable.SetRValReferenceFlag();

		variable.SetPointerAmount(PointerAmount);

		if constexpr (!std::is_same_v<void, Type_RemovedExtents>)
		{
			constexpr uint32_t ArraySize{ sizeof(T) / sizeof(Type_RemovedExtents) };
			variable.SetArraySize(ArraySize);
		}
		else
		{
			variable.SetArraySize(1);
		}

		return variable;
	}

	inline std::unordered_map<TypeId, TypeInfo>& GetTypeInfoMap()
	{
		return GetGlobalData().TypeInfoMap;
	}

	inline const std::unordered_map<TypeId, TypeInfo>& GetAllTypeInfo()
	{
		return GetTypeInfoMap();
	}

	template <typename Parent, typename Child>
	constexpr BaseClassInfo BaseClassInfo::Create()
	{
		return { TypeId::Create<Parent>(), GetClassOffset<Parent, Child>()};
	}

	template <typename ... Types>
	constexpr std::array<VariableId, sizeof...(Types)> GetVariableArray()
	{
		return std::array<VariableId, sizeof...(Types)> {VariableId::Create<Types>()...};
	}

	template <typename Tuple, size_t ... Index>
	constexpr std::array<VariableId,sizeof...(Index)> GetVariableArrayTupleHelper(std::index_sequence<Index...>)
	{
		return std::array<VariableId, sizeof...(Index)> { VariableId::Create<std::tuple_element_t<Index, Tuple>>()...};
	}

	template <typename Tuple>
	constexpr std::array<VariableId, std::tuple_size_v<Tuple>> GetVariableArrayTuple()
	{
		constexpr size_t size = std::tuple_size_v<Tuple>;
		return GetVariableArrayTupleHelper<Tuple>(std::make_index_sequence<size>{});
	}

	template <typename T>
	const TypeInfo& RegisterType()
	{
		auto& typeInfoMap = GetTypeInfoMap();
		
		constexpr TypeId hash = TypeId::Create<T>();

		const auto it = typeInfoMap.find(hash);
		if (it == typeInfoMap.end())
		{
			return typeInfoMap.emplace(
				hash,
				TypeInfo::Create<T>()
			).first->second;
		}
		return it->second;
	}

	inline const TypeInfo& TypeId::GetInfo() const
	{
		return GetTypeInfo(*this);
	}

	inline const MemberInfo* TypeId::GetMemberInfo(size_t offset) const
	{
		auto& members = GetInfo().Members;

		MemberInfo info{};
		info.Offset = static_cast<uint32_t>(offset);

		const auto it = std::lower_bound(members.begin(), members.end(), info);
		if (it != members.end() && it->Offset == offset)
		{
			return &*it;
		}
		return nullptr;
	}

	inline const TypeInfo& GetTypeInfo(TypeId id)
	{
		assert(GetTypeInfoMap().contains(id));
		return GetTypeInfoMap()[id];
	}

	template<typename T>
	const TypeInfo& GetTypeInfo()
	{
		return GetTypeInfo(TypeId::Create<T>());
	}

	template <typename Class>
	const MemberInfo& RegisterField(VariableId memberId, std::string_view fieldName, uint32_t offset, uint32_t size, uint32_t align, MemberProperties properties)
	{
		MemberInfo info;
		info.Name			= fieldName;
		info.VariableId		= memberId;
		info.Offset			= offset;
		info.Size			= size;
		info.Align			= align;
		info.Properties		= properties;

		auto& memberInfo = const_cast<TypeInfo&>(RegisterType<Class>());

		return *memberInfo.Members.emplace(std::upper_bound(memberInfo.Members.begin(), memberInfo.Members.end(), info), std::move(info));
	}

	template<typename Class, typename Field>
	const MemberInfo& RegisterField(std::string_view fieldName, uint32_t offset, MemberProperties properties)
	{
		auto registerField = RegisterType<Field>();

		return RegisterField<Class>(
			TypeId::Create<Class>(),
			VariableId::Create<Field>(),
			fieldName,
			offset,
			sizeof(Field),
			alignof(Field),
			properties);
	}

	/** FUNCTION REFLECTION*/

	template <typename ReturnType, typename ... ParameterTypes>
	inline FunctionInfo FillFunctionInfo(const void* address, std::string_view name, FunctionProperties properties)
	{
		FunctionInfo info{};

		info.FunctionAddress	= address;
		info.ReturnType			= VariableId::Create<ReturnType>();
		info.Name				= name;
		info.TypesHash			= GetTypesHash<ReturnType, ParameterTypes...>();
		info.Properties			= properties;

		if constexpr (sizeof...(ParameterTypes) != 0)
		{
			info.ParameterTypes.resize(sizeof...(ParameterTypes));
			auto parameterTypes = GetVariableArray<ParameterTypes...>();

			std::copy(parameterTypes.begin(), parameterTypes.end(), info.ParameterTypes.begin());
		}

		return info;
	}

	template <typename TReturnType, typename ... TParameterTypes>
	FunctionInfo FunctionInfo::Create(TReturnType(*function)(TParameterTypes...), std::string_view name, FunctionProperties properties)
	{
		return FillFunctionInfo<TReturnType, TParameterTypes...>(reinterpret_cast<const void*&>(function), name, properties);
	}

	template <typename Class, typename TReturnType, typename ... TParameterTypes>
	FunctionInfo FunctionInfo::Create(TReturnType(Class::* function)(TParameterTypes...), std::string_view name, FunctionProperties properties)
	{
		auto info = FillFunctionInfo<TReturnType, TParameterTypes...>(reinterpret_cast<const void*&>(function), name, properties);
		info.OwningType = TypeId::Create<Class>();
		return info;
	}

	template <typename Class, typename TReturnType, typename ... TParameterTypes>
	FunctionInfo FunctionInfo::Create(TReturnType(Class::* function)(TParameterTypes...) const, std::string_view name, FunctionProperties properties)
	{
		auto info = FillFunctionInfo<TReturnType, TParameterTypes...>(reinterpret_cast<const void*&>(function), name, properties);
		info.OwningType = TypeId::Create<Class>();
		return info;
	}


	template <typename ReturnT, typename ... ParameterTs>
	auto FunctionInfo::Cast() const -> ReturnT(*)(ParameterTs...)
	{
		constexpr uint64_t typesHash = GetTypesHash<ReturnT, ParameterTs...>();

		return (TypesHash == typesHash) ?
			reinterpret_cast<ReturnT(*)(ParameterTs...)>(FunctionAddress) :
			nullptr;
	}

	template <typename Class, typename ReturnT, typename ... ParameterTs>
	auto FunctionInfo::MethodCast() const->ReturnT(Class::*)(ParameterTs...)
	{
		constexpr uint64_t typesHash = GetTypesHash<ReturnT, Class*, ParameterTs...>();

		ReturnT(Class:: * function)(ParameterTs...);
		function = reinterpret_cast<const decltype(function)&>(FunctionAddress);

		return (TypesHash == typesHash && OwningType == TypeId::Create<Class>()) ?
			function : nullptr;
	}

	inline std::string VariableId::ToString() const
	{
		std::string name = std::string(m_Type.GetInfo().Name);

		if (IsVolatile()) name = "volatile " + name;
		if (IsConst()) name = "const " + name;

		const uint32_t pointerAmount = GetPointerAmount();
		for (uint32_t i{}; i < pointerAmount; ++i)
		{
			name += '*';
		}

		if (IsRValReference()) name += "&&";
		else if (IsReference()) name += '&';

		return name;
	}

	inline const FunctionInfo* FunctionId::GetInfo() const
	{
		auto& functionMap = GetGlobalData().FunctionInfoMap;
		const auto it = functionMap.find(*this);
		if (it != functionMap.end())
		{
			return &it->second;
		}
		return nullptr;
	}

	template <typename ReturnType, typename ... ParameterTypes>
	auto FunctionId::Cast() const -> ReturnType(*)(ParameterTypes...)
	{
		return GetInfo()->Cast<ReturnType, ParameterTypes...>();
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	auto FunctionId::MethodCast() const->ReturnType(Class::*)(ParameterTypes...)
	{
		if (const auto info = GetInfo()) return info->MethodCast<Class, ReturnType, ParameterTypes...>();
		return nullptr;
	}

	template <typename ReturnType, typename ... ParameterTypes>
	FunctionId FunctionId::Create(ReturnType(*function)(ParameterTypes...), std::string_view name)
	{
		return FunctionId{ GetFunctionHash(function, name) };
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	FunctionId FunctionId::Create(ReturnType(Class::* function)(ParameterTypes...), std::string_view name)
	{
		return FunctionId{ GetFunctionHash(function, name) };
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	FunctionId FunctionId::Create(ReturnType(Class::* function)(ParameterTypes...) const, std::string_view name)
	{
		return FunctionId{ GetFunctionHash(function, name) };
	}

	constexpr bool FunctionInfo::IsPropertySet(FunctionProperties property) const
	{
		return !!(Properties & property);
	}

	template <typename ReturnType, typename ... ParameterTypes>
	FunctionId FunctionId::GetFunctionId(ReturnType(*function)(ParameterTypes...))
	{
		return GetFunctionId(reinterpret_cast<const void*&>(function));
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	FunctionId FunctionId::GetFunctionId(ReturnType(Class::* function)(ParameterTypes...))
	{
		return GetFunctionId(reinterpret_cast<const void*&>(function));
	}

	template<typename Class, typename ReturnType, typename ...ParameterTypes>
	FunctionId FunctionId::GetFunctionId(ReturnType(Class::* function)(ParameterTypes...) const)
	{
		return GetFunctionId(reinterpret_cast<const void*&>(function));
	}

	inline FunctionId FunctionId::GetFunctionId(const void* functionAddress)
	{
		return GetGlobalData().FunctionAddressToIdMap[functionAddress];
	}

#ifdef GLAS_STORAGE
	inline void FunctionInfo::Call(Storage::TypeTuple& parameters, void* pReturnValue) const
	{
		assert(parameters.GetVariableIds().size() == ParameterTypes.size());
		assert(std::equal(ParameterTypes.begin(), ParameterTypes.end(), parameters.GetVariableIds().begin(), [](const VariableId& lhs, const VariableId& rhs)
		{
				return lhs.GetTypeId() == rhs.GetTypeId() &&
					lhs.GetArraySize() == rhs.GetArraySize() &&
					lhs.IsConst() == rhs.IsConst();
		}));
		FunctionCaller(FunctionAddress, parameters, pReturnValue);
	}

	inline void FunctionInfo::MemberCall(void* subject, Storage::TypeTuple& parameters, void* pReturnValue) const
	{
 		*static_cast<void**>(parameters.GetVoid(0)) = subject;
		Call(parameters, pReturnValue);
	}

	inline void FunctionId::Call(Storage::TypeTuple& parameters, void* pReturnValue) const
	{
		assert(GetInfo());
		GetInfo()->Call(parameters, pReturnValue);
	}

	inline void FunctionId::MemberCall(void* subject, Storage::TypeTuple& parameters, void* pReturnValue) const
	{
		assert(GetInfo());
		GetInfo()->MemberCall(subject, parameters, pReturnValue);
	}

	template <typename ParameterTypesTuple, typename Function, size_t... Index>
	auto TupleFunctionCall(Function function, Storage::TypeTuple& typeTuple, std::index_sequence<Index...>)
	{
		return function(
			typeTuple.Get<
			std::remove_reference_t<
			std::tuple_element_t<Index, ParameterTypesTuple>>>(Index)...);
	}

	template <typename ParameterTypesTuple, typename Class, typename Function, size_t... Index>
	auto TupleMethodCall(Function function, Storage::TypeTuple& typeTuple, std::index_sequence<Index...>)
	{
		return (typeTuple.Get<Class*>(0)->*function)(
			typeTuple.Get<
			std::remove_reference_t<
			std::tuple_element_t<Index + 1, ParameterTypesTuple>>>(Index + 1)...);
	}

	template <typename ReturnType, typename ... ParameterTypes>
	inline void FunctionCaller(const void* address, Storage::TypeTuple& tupleStorage, void* returnAddress)
	{
		ReturnType(*function)(ParameterTypes...);
		function = reinterpret_cast<decltype(function)&>(address);
		if constexpr (std::is_same_v<ReturnType, void>)
		{
			(void)returnAddress;

			TupleFunctionCall<std::tuple<ParameterTypes...>>(
				function,
				tupleStorage,
				std::make_index_sequence<sizeof...(ParameterTypes)>());
		}
		else
		{
			if (returnAddress)
				*static_cast<ReturnType*>(returnAddress) = TupleFunctionCall<std::tuple<ParameterTypes...>>(
					function,
					tupleStorage,
					std::make_index_sequence<sizeof...(ParameterTypes)>());
			else
				TupleFunctionCall<std::tuple<ParameterTypes...>>(
					function,
					tupleStorage,
					std::make_index_sequence<sizeof...(ParameterTypes)>());
		}
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	inline void MethodCaller(const void* address, Storage::TypeTuple& tupleStorage, void* returnAddress)
	{
		ReturnType(Class::*function)(ParameterTypes...);
		function = reinterpret_cast<decltype(function)&>(address);
		if constexpr (std::is_same_v<ReturnType, void>)
		{
			(void)returnAddress;

			TupleMethodCall<std::tuple<Class, ParameterTypes...>, Class>(
				function,
				tupleStorage,
				std::make_index_sequence<sizeof...(ParameterTypes)>());
		}
		else
		{
			if (returnAddress)
				*static_cast<ReturnType*>(returnAddress) = TupleMethodCall<std::tuple<Class, ParameterTypes...>, Class>(
					function,
					tupleStorage,
					std::make_index_sequence<sizeof...(ParameterTypes)>());
			else
				TupleMethodCall<std::tuple<Class, ParameterTypes...>, Class>(
					function,
					tupleStorage,
					std::make_index_sequence<sizeof...(ParameterTypes)>());
		}
	}

#else
	template <typename ReturnType, typename ... ParameterTypes>
	inline void FunctionCaller(const void* address, Storage::TypeTuple& tupleStorage, void* returnAddress)
	{
		(void)address;
		(void)tupleStorage;
		(void)returnAddress;
	}

	template <typename ReturnType, typename ... ParameterTypes>
	inline void MethodCaller(const void* address, Storage::TypeTuple& tupleStorage, void* returnAddress)
	{
		(void)address;
		(void)tupleStorage;
		(void)returnAddress;
	}
#endif

	template <typename ReturnType, typename ... ParameterTypes>
	const FunctionInfo& RegisterFunction(ReturnType(*function)(ParameterTypes...), 
		std::string_view name,
		FunctionProperties properties)
	{
		FunctionId functionId = FunctionId::Create(function, name);
		
		if (const FunctionInfo* functionInfo = functionId.GetInfo())
			return *functionInfo;

		FunctionInfo info = FunctionInfo::Create(function, name, properties & ~(FunctionProperties::ConstMethod | FunctionProperties::Method));
		info.FunctionCaller = &FunctionCaller<ReturnType, ParameterTypes...>;

		auto& globalFunctionData = GetGlobalData();

		globalFunctionData.NameToFunctionIdMap.emplace(name, functionId);
		globalFunctionData.FunctionAddressToIdMap.emplace(info.FunctionAddress, functionId);
		return globalFunctionData.FunctionInfoMap.emplace(functionId, std::move(info)).first->second;
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	const FunctionInfo& RegisterMethodFunctionHelper(FunctionId functionId, const void* function, std::string_view name, FunctionProperties properties)
	{
		if (const FunctionInfo* functionInfo = functionId.GetInfo())
			return *functionInfo;

		FunctionInfo info = FillFunctionInfo<ReturnType, Class*, ParameterTypes...>(function, name, properties);
		info.OwningType = TypeId::Create<Class>();
		info.FunctionCaller = &MethodCaller<Class, ReturnType, ParameterTypes...>;

		auto& classInfo = const_cast<TypeInfo&>(RegisterType<Class>());
		classInfo.MemberFunctions.emplace_back(functionId);

		auto& globalFunctionData = GetGlobalData();

		globalFunctionData.NameToFunctionIdMap.emplace(name, functionId);
		globalFunctionData.FunctionAddressToIdMap.emplace(info.FunctionAddress, functionId);
		return globalFunctionData.FunctionInfoMap.emplace(functionId, std::move(info)).first->second;
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	const FunctionInfo& RegisterMethodFunction(ReturnType(Class::* function)(ParameterTypes...), 
		std::string_view name,
		FunctionProperties properties = FunctionProperties::None)
	{
		return RegisterMethodFunctionHelper<Class, ReturnType, ParameterTypes...>(
			FunctionId::Create(function, name),
			reinterpret_cast<const void*&>(function),
			name,
			(properties | FunctionProperties::Method) & ~(FunctionProperties::ConstMethod));
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	const FunctionInfo& RegisterConstMethodFunction(ReturnType(Class::* function)(ParameterTypes...) const, 
		std::string_view name, 
		FunctionProperties properties = FunctionProperties::None)
	{
		return RegisterMethodFunctionHelper<Class, ReturnType, ParameterTypes...>(
			FunctionId::Create(function, name),
			reinterpret_cast<const void*&>(function),
			name,
			(properties | FunctionProperties::ConstMethod) & ~(FunctionProperties::Method));
	}

	template <typename... Types>
	constexpr uint64_t GetTypesHash()
	{
		constexpr auto variableIds = GetVariableArray<Types...>();

		std::array<uint64_t, sizeof...(Types)> variableHashes{};

		std::transform(variableIds.begin(), variableIds.end(), variableHashes.begin(), [](VariableId id)
			{
				return id.GetHash();
			});

		return hash({ variableHashes.data(), variableHashes.size() });
	}

	template <typename ReturnType, typename ... ParameterTypes>
	uint64_t GetFunctionHash(ReturnType(*)(ParameterTypes...), std::string_view name)
	{
		return hash(name) ^ GetTypesHash<ReturnType, ParameterTypes...>();
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	uint64_t GetFunctionHash(ReturnType(Class::*)(ParameterTypes...), std::string_view name)
	{
		return hash(name) ^ GetTypesHash<Class, ReturnType, ParameterTypes...>();
	}

	template <typename Class, typename ReturnType, typename ... ParameterTypes>
	uint64_t GetFunctionHash(ReturnType(Class::*)(ParameterTypes...) const, std::string_view name)
	{
		return hash(name) ^ GetTypesHash<Class, ReturnType, ParameterTypes...>();
	}

	template <typename Parent, typename Child>
	constexpr size_t GetClassOffset()
	{
		static_assert(std::is_base_of_v<Parent, Child>);
		Child* pChild{ reinterpret_cast<Child*>(0xBEEF) }; // does not work if we use nullptr
		return reinterpret_cast<char*>(static_cast<Parent*>(pChild)) - reinterpret_cast<char*>(pChild);
	}

	template <typename Parent, typename Child>
	constexpr void RegisterChild()
	{
		auto& parentInfo = const_cast<TypeInfo&>(RegisterType<Parent>());
		auto& childInfo = const_cast<TypeInfo&>(RegisterType<Child>());

		assert(parentInfo.ChildClasses.end() == std::ranges::find(parentInfo.ChildClasses, TypeId::Create<Child>()));
		assert(childInfo.BaseClasses.end() == std::ranges::find_if(childInfo.BaseClasses, [](BaseClassInfo info) { return info.BaseId == TypeId::Create<Parent>(); }));

		parentInfo.ChildClasses.emplace_back(TypeId::Create<Child>());
		childInfo.BaseClasses.emplace_back(BaseClassInfo::Create<Parent, Child>());
	}
}
