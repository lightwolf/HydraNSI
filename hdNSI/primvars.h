#ifndef HDNSI_PRIMVARS_H
#define HDNSI_PRIMVARS_H

#include <pxr/imaging/hd/sceneDelegate.h>
#include <pxr/imaging/hd/timeSampleArray.h>

#include <nsi.hpp>

#include <string>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIRenderParam;

/*
	This class handles primvar export on an rprim.
*/
class HdNSIPrimvars
{
public:
	/**
		\param keep_points
			If true, a reference is kept on exported points so they can be used
			by external code. This is meant for the mesh normal generation.
	*/
	HdNSIPrimvars(bool keep_points)
	:
		m_keep_points{keep_points}
	{
	}

	class SampleArray : public HdTimeSampleArray<VtValue, 4>
	{
	public:
		SampleArray() = default;
		/* Convenience constructor for the case with no motion blur. */
		SampleArray(const VtValue &value)
		{
			Resize(1);
			times[0] = 0.0f;
			values[0] = value;
		}
	};

	void Sync(
		HdSceneDelegate *sceneDelegate,
		HdNSIRenderParam *renderParam,
		HdDirtyBits *dirtyBits,
		NSI::Context &nsi,
		const SdfPath &primId,
		const std::string &geoHandle,
		const VtIntArray &vertexIndices );

	static bool SetAttributeFromValue(
		NSI::Context &nsi,
		const std::string &nodeHandle,
		const HdPrimvarDescriptor &primvar,
		const VtValue &value,
		int flags,
		double sample_time,
		bool use_time);

	bool HasNormals() const { return m_has_normals; }
	const VtVec3fArray& GetPoints() const { return m_points; }

private:
	void SetOnePrimvar(
		HdSceneDelegate *sceneDelegate,
		NSI::Context &nsi,
		const SdfPath &primId,
		const std::string &geoHandle,
		const VtIntArray &vertexIndices,
		const HdPrimvarDescriptor &primvar,
		const SampleArray &values);

	void SetObjectAttributes(
		HdSceneDelegate *sceneDelegate,
		NSI::Context &nsi,
		const SdfPath &primId,
		const std::string &geoHandle,
		const HdPrimvarDescriptor &primvar);

	void SetVisibilityAttributes(
		HdSceneDelegate *sceneDelegate,
		NSI::Context &nsi,
		const SdfPath &primId,
		const std::string &geoHandle,
		const HdPrimvarDescriptor &primvar);

	/* Track if we exported the normals primvar. */
	bool m_has_normals{false};
	/* If true, keep a reference to exported points. */
	bool m_keep_points{false};
	/* The exported points, if above var is true. */
	VtVec3fArray m_points;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
