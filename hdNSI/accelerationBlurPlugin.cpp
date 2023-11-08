#include "accelerationBlurPlugin.h"

#include <pxr/imaging/hd/filteringSceneIndex.h>
#include <pxr/imaging/hd/primvarsSchema.h>
#include <pxr/imaging/hd/retainedDataSource.h>
#include <pxr/imaging/hd/sceneIndexPluginRegistry.h>
#include <pxr/imaging/hd/tokens.h>

#include <algorithm>

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PRIVATE_TOKENS(
	_tokens,
	((pluginId, "HdNSIAccelerationBlurPlugin"))
	(quadraticmotion)
);


TF_REGISTRY_FUNCTION(TfType)
{
	HdSceneIndexPluginRegistry::Define<
		HdNSIAccelerationBlurPlugin, HdSceneIndexPlugin>();
}

TF_REGISTRY_FUNCTION(HdSceneIndexPlugin)
{
    HdSceneIndexPluginRegistry::GetInstance().RegisterSceneIndexForRenderer(
		/* This must match the renderer plugin's displayName in json */
		"3Delight",
		_tokens->pluginId,
		nullptr, /* inputArgs */
		HdSceneIndexPluginRegistry::InsertionPhase(0),
		HdSceneIndexPluginRegistry::InsertionOrderAtStart);
}

namespace
{

/* Holds stuff we want to pass around deep in the data source hierarchy. */
struct Args
{
	double m_fps;
};

/*
	This should be a HdRetainedSampledDataSource but whatever is consuming this
	isn't using the HdSampledDataSource interface correctly. It produces no
	primvar if outSampleTimes is left empty when returning false.
*/
class ABFixedValueDataSource final : public HdSampledDataSource
{
public:
	HD_DECLARE_DATASOURCE(ABFixedValueDataSource);

	ABFixedValueDataSource(VtValue &&i_value) : m_value(i_value) {}

	VtValue GetValue(Time shutterOffset) override
	{
		return m_value;
	}

	bool GetContributingSampleTimesForInterval(
		Time startTime,
		Time endTime,
		std::vector<Time> *outSampleTimes) override
	{
		*outSampleTimes = {0.0f};
		return false;
	}

private:
	VtValue m_value;
};

/*
	This does the actual calculation of new point samples.
*/
class ABPointsValueDataSource final : public HdSampledDataSource
{
public:
	HD_DECLARE_DATASOURCE(ABPointsValueDataSource);

	ABPointsValueDataSource(
		const HdSampledDataSourceHandle &i_source,
		const HdContainerDataSourceHandle &i_primvarsSource,
		const Args &i_args)
	:
		m_source(i_source),
		m_primvarsSource(i_primvarsSource),
		m_args(i_args)
	{
	}
		
	VtValue GetValue(Time shutterOffset) override
	{
		VtValue vt_vel = GetV3fPrimvarValue(HdTokens->velocities);
		/* We don't support acceleration only. It makes little sense. */
		if( vt_vel.IsEmpty() )
		{
			return m_source->GetValue(shutterOffset);
		}
		VtValue vt_points = GetV3fPrimvarValue(HdTokens->points);
		if( vt_points.IsEmpty() )
		{
			return m_source->GetValue(shutterOffset);
		}
		const VtVec3fArray &points = vt_points.UncheckedGet<VtVec3fArray>();
		const VtVec3fArray &vel = vt_vel.UncheckedGet<VtVec3fArray>();
		if( vel.size() != points.size() )
		{
			return m_source->GetValue(shutterOffset);
		}

		/* Shutter offset is in frames, velocities are per second. */
		float t = shutterOffset / m_args.m_fps;
		VtVec3fArray p = vt_points.UncheckedGet<VtVec3fArray>();
		size_t n = p.size();

		VtValue vt_acc = GetV3fPrimvarValue(HdTokens->accelerations);
		if( !vt_acc.IsEmpty() )
		{
			const VtVec3fArray &acc = vt_acc.UncheckedGet<VtVec3fArray>();
			if( acc.size() == points.size() )
			{
				/* Use acceleration and velocity. */
				float ht2 = 0.5f * t * t;
				for( size_t i = 0; i < n; ++i )
				{
					p[i] += t * vel[i] + ht2 * acc[i];
				}
				return VtValue{p};
			}
		}

		/* Use velocity only. */
		for( size_t i = 0; i < n; ++i )
		{
			p[i] += t * vel[i];
		}
		return VtValue{p};
	}

	bool GetContributingSampleTimesForInterval(
		Time startTime,
		Time endTime,
		std::vector<Time> *outSampleTimes) override
	{
		VtValue velocities = GetV3fPrimvarValue(HdTokens->velocities);
		if( velocities.IsEmpty() )
		{
			return m_source->GetContributingSampleTimesForInterval(
				startTime, endTime, outSampleTimes);
		}

		if( startTime < -1e-6f || endTime > 1e6f )
		{
			/* Bogus range. Infer one from the samples of our source. */
			if( !m_source->GetContributingSampleTimesForInterval(
					startTime, endTime, outSampleTimes) )
			{
				return false;
			}

			auto mm = std::minmax_element(
				outSampleTimes->begin(), outSampleTimes->end());
			if( mm.first == mm.second || *mm.first == *mm.second )
				return false;

			startTime = *mm.first;
			endTime = *mm.second;
		}

		outSampleTimes->clear();
		VtValue accelerations = GetV3fPrimvarValue(HdTokens->accelerations);
		outSampleTimes->push_back(startTime);
		if( !accelerations.IsEmpty() )
		{
			outSampleTimes->push_back(0.5f * (startTime + endTime));
		}
		outSampleTimes->push_back(endTime);

		return true;
	}

private:
	VtValue GetV3fPrimvarValue(const TfToken &i_name)
	{
		HdDataSourceBaseHandle ds = HdContainerDataSource::Get(m_primvarsSource,
			HdDataSourceLocator(i_name, HdPrimvarSchemaTokens->primvarValue));
		HdSampledDataSourceHandle sds = HdSampledDataSource::Cast(ds);
		if( !sds )
			return {};
		VtValue v = sds->GetValue(0.0f);
		if( !v.IsHolding<VtVec3fArray>() )
			return {};
		return v;

	}

private:
	HdSampledDataSourceHandle m_source;
	/* Needed to access other primvars. */
	HdContainerDataSourceHandle m_primvarsSource;
	const Args m_args;
};

class ABPointsDataSource final : public HdContainerDataSource
{
public:
	HD_DECLARE_DATASOURCE(ABPointsDataSource);

	ABPointsDataSource(
		const HdContainerDataSourceHandle &i_source,
		const HdContainerDataSourceHandle &i_primvarsSource,
		const Args &i_args)
	:
		m_source(i_source),
		m_primvarsSource(i_primvarsSource),
		m_args(i_args)
	{
	}

#if PXR_VERSION < 2302
	bool Has(const TfToken &name) override
	{
		return m_source->Has(name);
	}
#endif

	TfTokenVector GetNames() override
	{
		return m_source->GetNames();
	}

	HdDataSourceBaseHandle Get(const TfToken &name) override
	{
		HdDataSourceBaseHandle h = m_source->Get(name);
		if( name == HdPrimvarSchemaTokens->primvarValue )
		{
			HdSampledDataSourceHandle sh = HdSampledDataSource::Cast(h);
			if( sh ) 
			{
				return ABPointsValueDataSource::New(
					sh, m_primvarsSource, m_args);
			}
		}
		return h;
	}

private:
	HdContainerDataSourceHandle m_source;
	/* Needed to access other primvars. */
	HdContainerDataSourceHandle m_primvarsSource;
	const Args m_args;
};

/*
	This will:
	- Apply velocies and accelerations to points (for multiple samples).
	- Add quadraticmotion if doing the above.
*/
class ABPrimvarsDataSource final : public HdContainerDataSource
{
public:
	HD_DECLARE_DATASOURCE(ABPrimvarsDataSource);

	ABPrimvarsDataSource(
		const HdContainerDataSourceHandle &i_source,
		const Args &i_args)
	:
		m_source(i_source),
		m_args(i_args)
	{
	}

#if PXR_VERSION < 2302
	bool Has(const TfToken &name) override
	{
		if( name == _tokens->quadraticmotion )
		{
			return
				m_source->Has(HdTokens->velocities) &&
				m_source->Has(HdTokens->accelerations);
		}
		return m_source->Has(name);
	}
#endif

	TfTokenVector GetNames() override
	{
		TfTokenVector names = m_source->GetNames();
		unsigned removed = 0;
		for( auto it = names.begin(); it != names.end(); )
		{
			if( *it == HdTokens->velocities ||
			    *it == HdTokens->accelerations )
			{
				/* Remove those. */
				it = names.erase(it);
				++removed;
				continue;
			}
			++it;
		}
		if( removed >= 2 )
		{
			names.push_back(_tokens->quadraticmotion);
		}
		return names;
	}

	HdDataSourceBaseHandle Get(const TfToken &name) override
	{
		if( name == _tokens->quadraticmotion )
		{
			/* All this code is to say the value is 1 :] */
			return HdRetainedContainerDataSource::New(
				HdPrimvarSchemaTokens->primvarValue,
				ABFixedValueDataSource::New(VtValue(1)),
				HdPrimvarSchemaTokens->interpolation,
				HdRetainedTypedSampledDataSource<TfToken>::New(
					HdPrimvarSchemaTokens->constant),
				HdPrimvarSchemaTokens->role,
				HdRetainedTypedSampledDataSource<TfToken>::New(TfToken()));

		}
		HdDataSourceBaseHandle h = m_source->Get(name);
		if( name == HdTokens->points )
		{
			HdContainerDataSourceHandle ch = HdContainerDataSource::Cast(h);
			/*
				Checking for velocities here is redundant but it's a more
				efficient place to skip running our code when not needed.
				Which should be most of the time.
			*/
			if( ch && m_source->Get(HdTokens->velocities) )
				return ABPointsDataSource::New(ch, m_source, m_args);
		}
		return h;
	}

private:
	HdContainerDataSourceHandle m_source;
	const Args m_args;
};

class ABPrimDataSource final : public HdContainerDataSource
{
public:
	HD_DECLARE_DATASOURCE(ABPrimDataSource);

	ABPrimDataSource(
		const HdContainerDataSourceHandle &i_source,
		const Args &i_args)
	:
		m_source(i_source),
		m_args(i_args)
	{
	}

#if PXR_VERSION < 2302
	bool Has(const TfToken &name) override
	{
		return m_source->Has(name);
	}
#endif

	TfTokenVector GetNames() override
	{
		return m_source->GetNames();
	}

	HdDataSourceBaseHandle Get(const TfToken &name) override
	{
		HdDataSourceBaseHandle h = m_source->Get(name);
		if( name == HdPrimvarsSchemaTokens->primvars )
		{
			HdContainerDataSourceHandle ch = HdContainerDataSource::Cast(h);
			if( ch )
				return ABPrimvarsDataSource::New(ch, m_args);
		}
		return h;
	}

private:
	HdContainerDataSourceHandle m_source;
	const Args m_args;
};

HdDataSourceLocator PrimvarLocator(const TfToken &primvar)
{
	return {
		HdPrimvarsSchemaTokens->primvars, primvar,
		HdPrimvarSchemaTokens->primvarValue};
}

class ABSceneIndex final : public HdSingleInputFilteringSceneIndexBase
{
public:
	ABSceneIndex(
		const HdSceneIndexBaseRefPtr &inputSceneIndex,
		const Args &i_args)
	:
		HdSingleInputFilteringSceneIndexBase(inputSceneIndex),
		m_args(i_args)
	{
	}

	HdSceneIndexPrim GetPrim(const SdfPath &primPath) const override
	{
		HdSceneIndexPrim prim =  _GetInputSceneIndex()->GetPrim(primPath);
		if( !prim.dataSource )
			return prim;
		return HdSceneIndexPrim{
			prim.primType,
			ABPrimDataSource::New(prim.dataSource, m_args)};
	}

	SdfPathVector GetChildPrimPaths(const SdfPath &primPath) const override
	{
		return _GetInputSceneIndex()->GetChildPrimPaths(primPath);
	}

protected:
	void _PrimsAdded(
		const HdSceneIndexBase &sender,
		const HdSceneIndexObserver::AddedPrimEntries &entries) override
	{
		_SendPrimsAdded(entries);
	}

	virtual void _PrimsRemoved(
		const HdSceneIndexBase &sender,
		const HdSceneIndexObserver::RemovedPrimEntries &entries) override
	{
		_SendPrimsRemoved(entries);
	}

	virtual void _PrimsDirtied(
		const HdSceneIndexBase &sender,
		const HdSceneIndexObserver::DirtiedPrimEntries &entries) override
	{
		static const HdDataSourceLocator velocityLocator =
			PrimvarLocator(HdTokens->velocities);
		static const HdDataSourceLocatorSet consumedLocators{
			velocityLocator,
			PrimvarLocator(HdTokens->accelerations)};
		static const HdDataSourceLocatorSet producedLocators{
			PrimvarLocator(HdTokens->points),
			PrimvarLocator(_tokens->quadraticmotion)};

		HdSceneIndexObserver::DirtiedPrimEntries updated_entries;
		updated_entries.reserve(entries.size());
		for( const auto &entry : entries )
		{
			/* We only do our thing if velocities are available. */
			if( !entry.dirtyLocators.Intersects(velocityLocator) )
			{
				updated_entries.emplace_back(entry);
				continue;
			}
			/* Alter the list to reflect how we manipulate primvars. */
			updated_entries.emplace_back(
				entry.primPath, HdDataSourceLocatorSet{});
			for( const HdDataSourceLocator &locator : entry.dirtyLocators )
			{
				/* Remove velocity and acceleration. */
				if( consumedLocators.Intersects(locator) )
					continue;
				/* Keep everything else. */
				updated_entries.back().dirtyLocators.append(locator);
			}
			/* Add the primvars we change/generate. */
			updated_entries.back().dirtyLocators.insert(producedLocators);
		}

		_SendPrimsDirtied(updated_entries);
	}

private:
	Args m_args;
};

}

double HdNSIAccelerationBlurPlugin::m_fps{24.0};

/*
	Set frames per second for future instances of the scene index filter.
*/
void HdNSIAccelerationBlurPlugin::SetFPS(double fps)
{
	m_fps = fps;
}

HdSceneIndexBaseRefPtr HdNSIAccelerationBlurPlugin::_AppendSceneIndex(
	const HdSceneIndexBaseRefPtr &inputScene,
	const HdContainerDataSourceHandle &inputArgs)
{
	Args args;
	args.m_fps = m_fps;
	return TfCreateRefPtr(new ABSceneIndex(inputScene, args));
}

PXR_NAMESPACE_CLOSE_SCOPE
