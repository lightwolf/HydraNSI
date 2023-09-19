#ifndef HDNSI_ACCELERATION_BLUR_PLUGIN_H
#define HDNSI_ACCELERATION_BLUR_PLUGIN_H

#include <pxr/pxr.h>
#include <pxr/imaging/hd/sceneIndexPlugin.h>

PXR_NAMESPACE_OPEN_SCOPE

class HdNSIAccelerationBlurPlugin final : public HdSceneIndexPlugin
{
public:
	static void SetFPS(double fps);

protected:
	HdSceneIndexBaseRefPtr _AppendSceneIndex(
		const HdSceneIndexBaseRefPtr &inputScene,
		const HdContainerDataSourceHandle &inputArgs) override;

private:
	static double m_fps;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
