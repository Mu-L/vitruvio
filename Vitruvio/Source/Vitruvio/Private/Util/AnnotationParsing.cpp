#include "AnnotationParsing.h"

namespace
{
	constexpr const wchar_t* ANNOT_RANGE = L"@Range";
	constexpr const wchar_t* ANNOT_ENUM = L"@Enum";
	constexpr const wchar_t* ANNOT_HIDDEN = L"@Hidden";
	constexpr const wchar_t* ANNOT_COLOR = L"@Color";
	constexpr const wchar_t* ANNOT_DIR = L"@Directory";
	constexpr const wchar_t* ANNOT_FILE = L"@File";
	constexpr const wchar_t* ANNOT_ORDER = L"@Order";
	constexpr const wchar_t* ANNOT_GROUP = L"@Group";

	constexpr const wchar_t* NULL_KEY = L"#NULL#";
	constexpr const wchar_t* MIN_KEY = L"min";
	constexpr const wchar_t* MAX_KEY = L"max";
	constexpr const wchar_t* STEP_SIZE_KEY = L"stepsize";
	constexpr const wchar_t* RESTRICTED_KEY = L"restricted";
	
	UAttributeAnnotation* ParseEnumAnnotation(const prt::Annotation* Annotation)
	{
		return NewObject<UEnumAnnotation>();
	}

	UAttributeAnnotation* ParseRangeAnnotation(const prt::Annotation* Annotation)
	{
		URangeAnnotation* RangeAnnotation = NewObject<URangeAnnotation>();
		RangeAnnotation->Min = std::numeric_limits<double>::quiet_NaN();
		RangeAnnotation->Max = std::numeric_limits<double>::quiet_NaN();
		RangeAnnotation->StepSize = 0.1;
		
		for (int ArgIndex = 0; ArgIndex < Annotation->getNumArguments(); ArgIndex++)
		{
			const prt::AnnotationArgument* Argument = Annotation->getArgument(ArgIndex);
			const wchar_t* Key = Argument->getKey();
			if (std::wcscmp(Key, MIN_KEY) == 0)
			{
				RangeAnnotation->Min = Argument->getFloat();
			}
			else if (std::wcscmp(Key, MAX_KEY) == 0)
			{
				RangeAnnotation->Max = Argument->getFloat();
			}
			else if (std::wcscmp(Key, STEP_SIZE_KEY) == 0)
			{
				RangeAnnotation->StepSize = Argument->getFloat();
			}
			else if (std::wcscmp(Key, RESTRICTED_KEY) == 0)
			{
				RangeAnnotation->Restricted = Argument->getBool();
			}
		}
		
		return RangeAnnotation;
	}

	UAttributeAnnotation* ParseColorAnnotation(const prt::Annotation* Annotation)
	{
		return NewObject<UColorAnnotation>();
	}

	UAttributeAnnotation* ParseDirAnnotation(const prt::Annotation* Annotation)
	{
		return NewObject<UFilesystemAnnotation>();
	}

	UAttributeAnnotation* ParseFileAnnotation(const prt::Annotation* Annotation)
	{
		return NewObject<UFilesystemAnnotation>();
	}

	int ParseOrder(const prt::Annotation* Annotation)
	{
		return 0;
	}

	int ParseGroupOrder(const prt::Annotation* Annotation)
	{
		return 0;
	}
	
	FAttributeGroups ParseGroups(const prt::Annotation* Annotation)
	{
		return {};
	}
}

UAttributeMetadata* ParseAttributeMetadata(const prt::RuleFileInfo::Entry* AttributeInfo)
{
	UAttributeMetadata* Metadata = NewObject<UAttributeMetadata>();
	
	for (size_t AnnotationIndex = 0; AnnotationIndex < AttributeInfo->getNumAnnotations(); ++AnnotationIndex)
	{
		const prt::Annotation* CEAnnotation = AttributeInfo->getAnnotation(AnnotationIndex);
		
		const wchar_t* Name = CEAnnotation->getName();
		if (std::wcscmp(Name, ANNOT_ENUM) == 0)
		{
			Metadata->Annotation = ParseEnumAnnotation(CEAnnotation);
		}
		else if (std::wcscmp(Name, ANNOT_RANGE) == 0)
		{
			Metadata->Annotation = ParseRangeAnnotation(CEAnnotation);
		}
		else if (std::wcscmp(Name, ANNOT_COLOR) == 0)
		{
			Metadata->Annotation = ParseColorAnnotation(CEAnnotation);
		}
		else if (std::wcscmp(Name, ANNOT_DIR) == 0) {
			Metadata->Annotation = ParseDirAnnotation(CEAnnotation);
		}
		else if (std::wcscmp(Name, ANNOT_FILE) == 0)
		{
			Metadata->Annotation = ParseFileAnnotation(CEAnnotation);
		}

		if (!std::wcscmp(Name, ANNOT_HIDDEN))
		{
			Metadata->Hidden = true;
		}
		else if (!std::wcscmp(Name, ANNOT_ORDER))
		{
			Metadata->Order = ParseOrder(CEAnnotation);
		}
		else if (!std::wcscmp(Name, ANNOT_GROUP))
		{
			Metadata->Groups = ParseGroups(CEAnnotation);
			Metadata->GroupOrder = ParseGroupOrder(CEAnnotation);
		}
	}

	return Metadata;
}