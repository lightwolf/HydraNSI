#ifndef HDNSI_FIELD_H
#define HDNSI_FIELD_H

#include <pxr/imaging/hd/field.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIField : public HdField
{
public:
	HdNSIField(const SdfPath &id);
	virtual ~HdNSIField() override = default;

	HdNSIField(const HdNSIField&) = delete;
	void operator=(const HdNSIField&) = delete;

    virtual void Sync(
		HdSceneDelegate *sceneDelegate,
		HdRenderParam *renderParam,
		HdDirtyBits *dirtyBits) override;

	virtual HdDirtyBits GetInitialDirtyBitsMask() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
