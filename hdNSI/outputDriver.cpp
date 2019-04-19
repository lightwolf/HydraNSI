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

	// Find the pointer to preallocated handle
	Handle *imageHandle = nullptr;

	for (int i = 0; i < paramCount; ++i)
	{
		const UserParameter *parameter = parameters + i;

		const std::string param_name = parameter->name;
		if (param_name == "outputhandle") {
			imageHandle = ((Handle**)parameter->value)[0];
			break;
		}
	}

	if (imageHandle == nullptr) {
		return PkDspyErrorBadParams;
	}

	// Initialize the image handle.
	imageHandle->_width = width;
	imageHandle->_height = height;

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
	}

	imageHandle->_depth_buffer.resize(width * height,
	std::numeric_limits<float>::max());
	imageHandle->_buffer.resize(width * height * 4, 0);

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

	assert(entrySize == 8);
	int i = 0;

	for (int y = yMin; y < yMaxPlusOne; ++ y) {
		for (int x = xMin; x < xMaxPlusOne; ++ x) {
			size_t p = x + (imageHandle->_height - y - 1) * imageHandle->_width;
			size_t dstOffset = p * 4;

			imageHandle->_buffer[dstOffset + 0] = cdata[i * entrySize + 0];
			imageHandle->_buffer[dstOffset + 1] = cdata[i * entrySize + 1];
			imageHandle->_buffer[dstOffset + 2] = cdata[i * entrySize + 2];
			imageHandle->_buffer[dstOffset + 3] = cdata[i * entrySize + 3];

			float Ze = - *(float*)(cdata + i * entrySize + 4);
			// HdxCompositor wants a post-projection depth.
			float nd =
				(imageHandle->m_projM22 * Ze + imageHandle->m_projM32) / -Ze;
			imageHandle->_depth_buffer[p] = nd;

			++ i;
		}
	}

    return PkDspyErrorNone;
}

PtDspyError HdNSIOutputDriver::ImageClose(PtDspyImageHandle hImage)
{
	return PkDspyErrorNone;
}
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
