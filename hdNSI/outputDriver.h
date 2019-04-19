#ifndef HDNSI_OUTPUT_DRIVER_H
#define HDNSI_OUTPUT_DRIVER_H

#include <ndspy.h>
#include <nsi_dynamic.hpp>

class HdNSIOutputDriver
{
public:
	class Handle
	{
	public:
		int _width, _height;
		int _originalSizeX, _originalSizeY;
		int _originX, _originY;

		/*
			The elements of the projection matrix needed to compute an
			OpenGL like depth.
		*/
		double m_projM22{-0.5}, m_projM32{0.0};

		// Buffer for RGBA.
		std::vector<uint8_t> _buffer;
		// Buffer for Z.
		std::vector<float> _depth_buffer;
	};

	static void Register(NSI::DynamicAPI &api);

private:
	// Display Driver - Open callback function.
	static PtDspyError ImageOpen(
		PtDspyImageHandle *phImage,
		const char *driverName,
		const char *fileName,
		int width, int height,
		int paramCount,
		const UserParameter *parameters,
		int numFormats,
		PtDspyDevFormat formats[],
		PtFlagStuff *flagStuff);

	// Display Driver - Query callback function.
	static PtDspyError ImageQuery(
		PtDspyImageHandle hImage,
		PtDspyQueryType type,
		int dataLen,
		void *data);

	// Display Driver - Data callback function.
	static PtDspyError ImageData(
		PtDspyImageHandle hImage,
		int xMin, int xMaxPlusOne,
		int yMin, int yMaxPlusOne,
		int entrySize,
		const unsigned char *cdata);

	// Display Driver - Close callback function.
	static PtDspyError ImageClose(PtDspyImageHandle hImage);
};

#endif
// vim: set softtabstop=0 noexpandtab shiftwidth=4:
