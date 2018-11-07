#include "stdafx.h"
#include "DirectXRenderer.h"
#include "DirectXDevice.h"
#include <TDP/TouchPlugIn.h>
#include <array>

DirectXRenderer::DirectXRenderer()
    : Renderer(), myDevice()
{
}


DirectXRenderer::~DirectXRenderer()
{
    
}

bool DirectXRenderer::setup(HWND window)
{
	Renderer::setup(window);
	HRESULT result = myDevice.createDeviceResources();
	if (SUCCEEDED(result))
	{
		// Create window resources with no depth-stencil buffer
		result = myDevice.createWindowResources(getWindow(), false);
	}
    return SUCCEEDED(result);
}

void DirectXRenderer::resize(int width, int height)
{
	std::lock_guard<std::mutex> guard(myMutex);
	myDevice.resize();
}

void DirectXRenderer::stop()
{
    myLeftSideImages.clear();
    myRightSideImages.clear();
	myDevice.stop();
}

bool DirectXRenderer::render()
{
    std::lock_guard<std::mutex> guard(myMutex);
 
    myDevice.setRenderTarget();
    myDevice.clear(myBackgroundColor[0], myBackgroundColor[1], myBackgroundColor[2], 1.0f);

    float scale = 1.0f / (max(myLeftSideImages.size(), myRightSideImages.size()) + 1.0f);
    drawImages(myLeftSideImages, scale, -0.5f);
    drawImages(myRightSideImages, scale, 0.5f);

    myDevice.present();
    return true;
}

void DirectXRenderer::addLeftSideImage(const unsigned char * rgba, size_t bytesPerRow, int width, int height)
{
	std::lock_guard<std::mutex> guard(myMutex);

	DirectXTexture texture = myDevice.loadTexture(rgba, int32_t(bytesPerRow), width, height);

	myLeftSideImages.emplace_back(texture);
	myLeftSideImages.back().setup(myDevice);
}

TPTexture * DirectXRenderer::createLeftSideImage(size_t index)
{
	auto &texture = myLeftSideImages[index];
	return TPD3DTextureCreate(texture.getTexture());
}

void DirectXRenderer::clearLeftSideImages()
{
    std::lock_guard<std::mutex> guard(myMutex);

    myLeftSideImages.clear();
}

void DirectXRenderer::addRightSideImage()
{
    std::lock_guard<std::mutex> guard(myMutex);

    myRightSideImages.emplace_back();
    myRightSideImages.back().setup(myDevice);

	Renderer::addRightSideImage();
}

void DirectXRenderer::setRightSideImage(size_t index, TPTexture * texture)
{
	std::lock_guard<std::mutex> guard(myMutex);

	TPTexture *d3d = nullptr;
	if (texture && TPTextureGetType(texture) == TPTextureTypeD3D)
	{
		// Retain as we release d3d and texture
		d3d = TPRetain(texture);
	}
	else if (texture && TPTextureGetType(texture) == TPTextureTypeDXGI)
	{
		d3d = TPD3DTextureCreateFromDXGI(myDevice.getDevice(), texture);
	}
	else if (texture && TPTextureGetType(texture) == TPTextureTypeOpenGL)
	{
		// TODO: or document output is always TPTextureTypeDXGI
	}
	if (d3d)
	{
		DirectXTexture tex(TPD3DTextureGetTexture(d3d));

		myRightSideImages.at(index).update(tex);
	}
	else
	{
		myRightSideImages.at(index).update(DirectXTexture());
	}

	// TODO: work out lifetime of which what what?
	Renderer::setRightSideImage(index, d3d);
	//Renderer::setRightSideImage(index, texture);
	
	if (d3d)
	{
		TPRelease(&d3d);
	}
}

void DirectXRenderer::clearRightSideImages()
{
    std::lock_guard<std::mutex> guard(myMutex);

    myRightSideImages.clear();

	Renderer::clearRightSideImages();
}

void DirectXRenderer::drawImages(std::vector<DirectXImage>& images, float scale, float xOffset)
{
    float numImages = (1.0f / scale) - 1.0f;
    float spacing = 1.0f / numImages;
    float yOffset = 1.0f - spacing;
    for (auto &image : images)
    {
        image.scale(scale);
        image.position(xOffset, yOffset);
        image.draw(myDevice);
        yOffset -= spacing * 2;
    }
}
