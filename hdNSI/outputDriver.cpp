#include "outputDriver.h"

#include <cassert>
#include <limits>

void HdNSIOutputDriver::Register(NSI::DynamicAPI &api)
{
	// Retrieve the function pointer to register display driver.
	decltype(&DspyRegisterDriverTable) PDspyRegisterDriverTable = nullptr;

	// Load the 3Delight library again.
	if (!PDspyRegisterDriverTable) {
		api.LoadFunction(PDspyRegisterDriverTable, "DspyRegisterDriverTable");
	}

	// Register the display driver.
	//
	if (PDspyRegisterDriverTable) {
		PtDspyDriverFunctionTable table;
		memset(&table, 0, sizeof(table));

		table.Version = k_PtDriverCurrentVersion;
		table.pOpen = &ImageOpen;
		table.pQuery = &ImageQuery;
		table.pWrite = &ImageData;
		table.pClose = &ImageClose;

		PDspyRegisterDriverTable("HdNSI", &table);
	}
}

PtDspyError HdNSIOutputDriver::ImageOpen(
	PtDspyImageHandle *phImage,
	const char *driverName,
	const char *fileName,
	int width, int height,
	int paramCount,
	const UserParameter *parameters,
	int numFormats,
	PtDspyDevFormat formats[],
	PtFlagStuff *flagStuff)
{
	if(!phImage) {
		return PkDspyErrorBadParams;
	}

	// Find the pointer to preallocated renderBuffer
	using PXR_INTERNAL_NS::HdNSIRenderBuffer;
	HdNSIRenderBuffer *buffer = nullptr;

	for (int i = 0; i < paramCount; ++i)
	{
		const UserParameter *parameter = parameters + i;

		const std::string param_name = parameter->name;
		if (param_name == "buffer") {
			buffer = ((HdNSIRenderBuffer**)parameter->value)[0];
		}
	}

	if (buffer == nullptr)
	{
		return PkDspyErrorBadParams;
	}

	/* Minimal sanity check: number of components. */
	if (HdGetComponentCount(buffer->GetFormat()) != numFormats)
	{
		return PkDspyErrorBadParams;
	}

	Handle *imageHandle = new Handle;

	// Initialize the image handle.
	imageHandle->_width = width;
	imageHandle->_height = height;
	imageHandle->m_buffer = buffer;

	for(int i = 0;i < paramCount; ++ i)
	{
		const UserParameter *parameter = parameters + i;

		const std::string &param_name = parameter->name;
		if (param_name == "OriginalSize")
		{
			const int *originalSize = static_cast<const int *>(parameter->value);
			imageHandle->_originalSizeX = originalSize[0];
			imageHandle->_originalSizeY = originalSize[1];
		}
		else if (param_name == "origin")
		{
			const int *origin = static_cast<const int *>(parameter->value);
			imageHandle->_originX = origin[0];
			imageHandle->_originY = origin[1];
		}
		else if (param_name == "projectdepth")
		{
			imageHandle->m_project = *(ProjData**)parameter->value;
		}
	}

	*phImage = imageHandle;

	return PkDspyErrorNone;
}

PtDspyError HdNSIOutputDriver::ImageQuery(
	PtDspyImageHandle hImage,
	PtDspyQueryType type,
	int dataLen,
	void *data)
{
	if(!data && type != PkStopQuery)
	{
		return PkDspyErrorBadParams;
	}

	switch(type)
	{
		case PkOverwriteQuery:
		{
			PtDspyOverwriteInfo info;
			info.overwrite = 1;
			memcpy(data, &info, dataLen > (int)sizeof(info) ? sizeof(info) : (size_t)dataLen);

			break;
		}
		case PkProgressiveQuery:
		{
			if(dataLen < (int)sizeof(PtDspyProgressiveInfo))
			{
				return PkDspyErrorBadParams;
			}
			reinterpret_cast<PtDspyProgressiveInfo *>(data)->acceptProgressive = 1;

			break;
		}
		case PkThreadQuery:
		{
			PtDspyThreadInfo info;
			info.multithread = 1;

			assert(dataLen >= sizeof(info));
			memcpy(data, &info, sizeof(info));

			break;
		}

		default:
		{
			return PkDspyErrorUnsupported;
		}
	}

	return PkDspyErrorNone;
}

PtDspyError HdNSIOutputDriver::ImageData(
	PtDspyImageHandle hImage,
	int xMin, int xMaxPlusOne,
	int yMin, int yMaxPlusOne,
	int entrySize,
	const unsigned char *cdata)
{
	if (!entrySize || !cdata) {
		return PkDspyErrorStop;
	}

	Handle *imageHandle = reinterpret_cast<Handle *>(hImage);

	if (!imageHandle) {
		return PkDspyErrorStop;
	}

	assert(entrySize == HdDataSizeOfFormat(imageHandle->m_buffer->GetFormat()));
	auto bufferFormat = imageHandle->m_buffer->GetFormat();
	bool intConvert = PXR_INTERNAL_NS::HdFormatInt32 ==
		HdGetComponentFormat(bufferFormat);
	uint8_t *buffer = (uint8_t *)imageHandle->m_buffer->Map();

	for (int y = yMin; y < yMaxPlusOne; ++ y)
	{
		/* Hydra works with row 0 at the bottom. */
		int buffer_y = imageHandle->_height - y - 1;
		uint8_t *buf_out =
			buffer + entrySize * (buffer_y * imageHandle->_width + xMin);
		const uint8_t *buf_in =
			cdata + entrySize * (y - yMin) * (xMaxPlusOne - xMin);
		if (imageHandle->m_project)
		{
			const auto &pd = *imageHandle->m_project;
			/* Hydra expects a post-projection depth, which is nonlinear in
			   [-1, 1], remapped to [0,1] */
			for (int x = xMin; x < xMaxPlusOne; ++x)
			{
				int i = x - xMin;
				float Ze = - ((const float*)buf_in)[i];
				float nd = (pd.M22 * Ze + pd.M32) / -Ze;
#if defined(PXR_VERSION) && PXR_VERSION <= 1911
				((float*)buf_out)[i] = nd;
#else
				((float*)buf_out)[i] = (nd + 1.0f) * 0.5f;
#endif
			}
		}
		else if (intConvert)
		{
			/* Integer AOVs were rendered as float. Convert here. */
			int32_t *out = (int32_t*)buf_out;
			const float *in = (const float*)buf_in;
			int n = HdGetComponentCount(bufferFormat) * (xMaxPlusOne - xMin);
			for (int i = 0; i < n; ++i)
			{
				out[i] = static_cast<int>(in[i]);
			}
		}
		else
		{
			memcpy(buf_out, buf_in, entrySize * (xMaxPlusOne - xMin));
		}
	}
	imageHandle->m_buffer->Unmap();

    return PkDspyErrorNone;
}

PtDspyError HdNSIOutputDriver::ImageClose(PtDspyImageHandle hImage)
{
	Handle *imageHandle = reinterpret_cast<Handle *>(hImage);
	delete imageHandle;
	return PkDspyErrorNone;
}
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
