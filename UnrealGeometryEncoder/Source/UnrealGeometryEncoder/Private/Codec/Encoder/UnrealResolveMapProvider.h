#pragma once

#include <istream>
#include <string>

#include "RulePackage.h"
#include "Util/UnrealPRTUtils.h"
#include "prtx/ResolveMapProvider.h"
#include "prtx/ResolveMapProviderFactory.h"
#include "prtx/Singleton.h"

class UnrealResolveMapProvider : public prtx::ResolveMapProvider
{
public:
	static const std::wstring ID;
	static const std::wstring NAME;
	static const std::wstring DESCRIPTION;
	static const std::wstring SCHEME_UNREAL;

	UnrealResolveMapProvider() = default;
	~UnrealResolveMapProvider() = default;
	prt::ResolveMap const* createResolveMap(prtx::URIPtr uri) const override;
};

class UnrealResolveMapProviderFactory : public prtx::ResolveMapProviderFactory, public prtx::Singleton<UnrealResolveMapProviderFactory>
{
public:
	~UnrealResolveMapProviderFactory() override;

	UnrealResolveMapProvider* create() const override
	{
		return new UnrealResolveMapProvider();
	}

	const std::wstring& getID() const override
	{
		return UnrealResolveMapProvider::ID;
	};
	const std::wstring& getName() const override
	{
		return UnrealResolveMapProvider::NAME;
	}
	const std::wstring& getDescription() const override
	{
		return UnrealResolveMapProvider::DESCRIPTION;
	}
	float getMerit() const override
	{
		return 3.0f;
	}
	bool canHandleURI(prtx::URIPtr uri) const override;

	static UnrealResolveMapProviderFactory* createInstance()
	{
		return new UnrealResolveMapProviderFactory();
	}
};