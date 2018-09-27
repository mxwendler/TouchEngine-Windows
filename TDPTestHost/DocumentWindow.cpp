#include "stdafx.h"
#include "DocumentWindow.h"
#include "DocumentManager.h"
#include "Resource.h"
#include <codecvt>
#include <vector>
#include <array>

wchar_t *DocumentWindow::WindowClassName = L"DocumentWindow";
const int32_t DocumentWindow::InputChannelCount = 2;
const double DocumentWindow::InputSampleRate = 44100.0;
const int64_t DocumentWindow::InputSampleLimit = 44100 / 2;
const int64_t DocumentWindow::InputSamplesPerFrame = 44100 / 60;

HINSTANCE theInstance;

HRESULT DocumentWindow::registerClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wndClass;
    wndClass.cbSize = sizeof(WNDCLASSEX);
    wndClass.style = CS_DBLCLKS;
    wndClass.lpfnWndProc = DocumentWindow::WndProc;
    wndClass.cbClsExtra = 0;
    wndClass.cbWndExtra = 0;
    wndClass.hInstance = hInstance;
    wndClass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TDPTESTHOST));
    wndClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wndClass.lpszMenuName = nullptr;
    wndClass.lpszClassName = WindowClassName;
    wndClass.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));

    if (!RegisterClassExW(&wndClass))
    {
        DWORD dwError = GetLastError();
        if (dwError != ERROR_CLASS_ALREADY_EXISTS)
        {
            return HRESULT_FROM_WIN32(dwError);
        }
    }

    theInstance = hInstance;

    return S_OK;
}

LRESULT CALLBACK DocumentWindow::WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_COMMAND:
    {
        int wmId = LOWORD(wParam);
        switch (wmId)
        {
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }
    break;
    case WM_CLOSE:
    {
        HMENU menu = GetMenu(hWnd);
        if (menu)
        {
            DestroyMenu(menu);
        }
        DestroyWindow(hWnd);
        break;
    }
    case WM_DESTROY:
    {
        auto document = DocumentManager::sharedManager().lookup(hWnd);
        if (document)
        {
            document->myWindow = nullptr;
        }
        DocumentManager::sharedManager().didClose(hWnd);
        break;
    }
    case WM_LBUTTONUP:
    {
        auto document = DocumentManager::sharedManager().lookup(hWnd);
        if (document)
        {
            // document->cancelFrame();
        }
        break;
    }
    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return LRESULT();
}

void DocumentWindow::eventCallback(TPInstance * instance, TPEvent event, TPResult result, int64_t time_value, int32_t time_scale, void * info)
{
    DocumentWindow *window = static_cast<DocumentWindow *>(info);

    if (result != TPResultSuccess)
    {
        int i = 32;
    }
    switch (event)
    {
    case TPEventInstanceDidLoad:
        window->didLoad();
        break;
    case TPEventParameterLayoutDidChange:
        window->parameterLayoutDidChange();
        break;
    case TPEventFrameDidFinish:
        window->endFrame(time_value, time_scale, result);
        break;
    default:
        break;
    }
}

void DocumentWindow::parameterValueCallback(TPInstance * instance, TPScope scope, int32_t group, int32_t index, void * info)
{
    DocumentWindow *doc = static_cast<DocumentWindow *>(info);

    if (scope == TPScopeOutput)
    {
        TPParameterType type;
        TPResult result = TPInstanceParameterGetType(instance, scope, group, index, &type);
        if (result == TPResultSuccess)
        {
            switch (type)
            {
            case TPParameterTypeDouble:
            {
                double value;
                result = TPInstanceParameterGetDoubleValue(doc->myInstance, scope, group, index, TPParameterValueCurrent, &value, 1);
                break;
            }
            case TPParameterTypeInt32:
            {
                int32_t value;
                result = TPInstanceParameterGetInt32Value(doc->myInstance, scope, group, index, TPParameterValueCurrent, &value, 1);
                break;
            }
            case TPParameterTypeString:
            {
                char value[512];
                result = TPInstanceParameterGetStringValue(doc->myInstance, scope, group, index, TPParameterValueCurrent, value, 512);
                break;
            }
            case TPParameterTypeTexture:
            {
                TPTexture *texture = nullptr;
                result = TPInstanceParameterCopyTextureValue(doc->myInstance, scope, group, index, TPParameterValueCurrent, &texture);
                if (result == TPResultSuccess)
                {
                    size_t imageIndex = doc->myOutputParameterTextureMap[std::make_pair(group, index)].first;

                    TPTexture *d3d = nullptr;
                    if (texture && TPTextureGetType(texture) == TPTextureTypeD3D)
                    {
                        // Retain as we release <d3d> and <texture>
                        d3d = TPRetain(texture);
                    }
                    else if (texture && TPTextureGetType(texture) == TPTextureTypeDXGI)
                    {
                        // TODO: check multi-threading allowed on ID3D11Device here
                        d3d = TPD3DTextureCreateFromDXGI(doc->myDevice.getDevice(), texture);
                    }
                    else if (texture && TPTextureGetType(texture) == TPTextureTypeOpenGL)
                    {
                        // TODO: or document output is always TPTextureTypeDXGI
                    }
                    if (d3d)
                    {
                        DirectXTexture tex(TPD3DTextureGetTexture(d3d));
                        doc->myRenderer.replaceRightSideTexture(imageIndex, tex);
                        doc->myOutputParameterTextureMap[std::make_pair(group, index)].second = std::shared_ptr<TPTexture *>(new TPTexture *(d3d), [] (TPTexture **t) {
                            TPRelease(*t);
                            delete t;
                        });
                    }
                    else
                    {
                        doc->myRenderer.replaceRightSideTexture(imageIndex, DirectXTexture());
                    }
                    if (texture)
                    {
                        TPRelease(texture);
                    }
                }
                break;
            }
            case TPParameterTypeFloatStream:
            {
                double rate;
                int32_t channelCount;
                int64_t maxSamples;

                result = TPInstanceParameterGetStreamDescription(doc->myInstance, scope, group, index, &rate, &channelCount, &maxSamples);

                if (result == TPResultSuccess)
                {
                    std::vector <std::vector<float>> store(channelCount);
                    std::vector<float *> channels;

                    for (auto &vector : store)
                    {
                        vector.resize(maxSamples);
                        channels.emplace_back(vector.data());
                    }

                    int64_t length = maxSamples;
                    result = TPInstanceParameterGetOutputStreamValues(doc->myInstance, group, index, channels.data(), channels.size(), &length);
                    if (result == TPResultSuccess)
                    {
                        // Use the channel data here
                        if (length > 0 && channels.size() > 0)
                        {
                            doc->myLastStreamValue = store.back()[length - 1];
                        }
                    }
                }
            }
                break;
            default:
                break;
            }
        }
    }
}

void DocumentWindow::endFrame(int64_t time_value, int32_t time_scale, TPResult result)
{
    myInFrame = false;
}

DocumentWindow::DocumentWindow(std::wstring path)
    : myInstance(nullptr), myWindow(nullptr), myRenderer(myDevice), myDidLoad(false), myInFrame(false), myLastStreamValue(1.0f), myLastFloatValue(0.0)
{
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    std::string utf8 = converter.to_bytes(path);
    myInstance = TPInstanceCreate(utf8.c_str(), TPTimeInternal, eventCallback, parameterValueCallback, this);
}


DocumentWindow::~DocumentWindow()
{
    if (myInstance)
    {
        TPInstanceDestroy(myInstance);
    }
    myRenderer.stop();
    myDevice.stop();
    
    if (myWindow)
    {
        PostMessageW(myWindow, WM_CLOSE, 0, 0);
    }
}

const std::wstring DocumentWindow::getPath() const
{
    if (myInstance == nullptr)
    {
        return std::wstring();
    }
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
    return converter.from_bytes(TPInstanceGetPath(myInstance));
}

void DocumentWindow::openWindow(HWND parent)
{
    RECT rc;
    SetRect(&rc, 0, 0, 640, 480);
    AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, false);

    std::wstring path = getPath();
    myWindow = CreateWindowW(WindowClassName,
        path.data(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT,
        (rc.right - rc.left), (rc.bottom - rc.top),
        parent,
        nullptr,
        theInstance,
        0);

    HRESULT result;
    if (myWindow)
    {
        ShowWindow(myWindow, SW_SHOW);
        UpdateWindow(myWindow);
        result = S_OK;
    }
    else
    {
        DWORD error = GetLastError();
        result = HRESULT_FROM_WIN32(error);
    }
    if (SUCCEEDED(result))
    {
        result = myDevice.createDeviceResources();
    }
    if (SUCCEEDED(result))
    {
        // Create window resources with no depth-stencil buffer
        result = myDevice.createWindowResources(myWindow, false);
    }
    if (SUCCEEDED(result))
    {
        result = myRenderer.setup() ? S_OK : EIO;
    }
}

void DocumentWindow::parameterLayoutDidChange()
{
    myRenderer.clearLeftSideImages();
    myRenderer.clearRightSideImages();
    myOutputParameterTextureMap.clear();

    std::array<TPScope, 2> scopes{ TPScopeInput, TPScopeOutput };

    for (auto scope : scopes)
    {
        int32_t groups;
        TPResult result = TPInstanceGetParameterGroupCount(myInstance, scope, &groups);
        if (result == TPResultSuccess)
        {
            for (int32_t i = 0; i < groups; i++)
            {
                int32_t properties;
                result = TPInstanceGetParameterCount(myInstance, scope, i, &properties);
                if (result == TPResultSuccess)
                {
                    for (int32_t j = 0; j < properties; j++)
                    {
                        TPParameterType type;
                        TPResult result = TPInstanceParameterGetType(myInstance, scope, i, j, &type);
                        if (result == TPResultSuccess && type == TPParameterTypeFloatStream && scope == TPScopeInput)
                        {
                            result = TPInstanceParameterSetInputStreamDescription(myInstance, i, j, InputSampleRate, InputChannelCount, InputSampleLimit);
                        }
                        else if (result == TPResultSuccess && type == TPParameterTypeTexture)
                        {
                            if (scope == TPScopeInput)
                            {
                                std::array<unsigned char, 256 * 256 * 4> tex;

                                for (int y = 0; y < 256; y++)
                                {
                                    for (int x = 0; x < 256; x++)
                                    {
                                        tex[(y * 256 * 4) + (x * 4) + 0] = x;
                                        tex[(y * 256 * 4) + (x * 4) + 1] = 40;
                                        tex[(y * 256 * 4) + (x * 4) + 2] = y;
                                        tex[(y * 256 * 4) + (x * 4) + 3] = 255;
                                    }
                                }

                                DirectXTexture texture = myDevice.loadTexture(tex.data(), 256 * 4, 256, 256);
                                myRenderer.addLeftSideTexture(texture);
                            }
                            else
                            {
                                myRenderer.addRightSideImage();
                                myOutputParameterTextureMap[std::make_pair(i, j)] = std::make_pair(myRenderer.getRightSideImageCount() - 1, nullptr);
                            }
                        }
                    }
                }
            }
        }
    }    
}

void DocumentWindow::render()
{
    float color[4] = { myDidLoad ? 0.6f : 0.6f, myDidLoad ? 0.6f : 0.6f, myDidLoad ? 1.0f : 0.6f, 1.0f};
    if (myDidLoad && !myInFrame)
    {
        int32_t groups;
        TPResult result = TPInstanceGetParameterGroupCount(myInstance, TPScopeInput, &groups);
        if (result == TPResultSuccess)
        {
            int textureCount = 0;
            for (int32_t i = 0; i < groups; i++)
            {
                int32_t properties;
                result = TPInstanceGetParameterCount(myInstance, TPScopeInput, i, &properties);
                if (result == TPResultSuccess)
                {
                    for (int32_t j = 0; j < properties; j++)
                    {
                        TPParameterType type;
                        if (TPInstanceParameterGetType(myInstance, TPScopeInput, i, j, &type) == TPResultSuccess)
                        {
                            switch (type)
                            {
                            case TPParameterTypeDouble:
							{
								double d = fmod(myLastFloatValue, 1.0);
								result = TPInstanceParameterSetDoubleValue(myInstance, i, j, &d, 1);
								break;
							}
                            case TPParameterTypeInt32:
							{
								int v = static_cast<int>(myLastFloatValue * 00) % 100;
								result = TPInstanceParameterSetInt32Value(myInstance, i, j, &v, 1);
								break;
							}
                            case TPParameterTypeString:
                                result = TPInstanceParameterSetStringValue(myInstance, i, j, "test input");
                                break;
                            case TPParameterTypeTexture:
                            {
                                ID3D11Texture2D *t = myRenderer.getLeftSideImage(textureCount).getTexture();
                                TPD3DTexture *texture = TPD3DTextureCreate(t);
                                result = TPInstanceParameterSetTextureValue(myInstance, i, j, texture);
                                TPRelease(texture);

                                textureCount++;
                                break;
                            }
                            case TPParameterTypeFloatStream:
                            {
                                std::array<float, InputSamplesPerFrame> channel;
                                std::fill(channel.begin(), channel.end(), static_cast<float>(fmod(myLastFloatValue, 1.0)));
                                std::array<const float *, InputChannelCount> channels;
                                std::fill(channels.begin(), channels.end(), channel.data());
                                int64_t filled = channel.size();
                                result = TPInstanceParameterAppendStreamValues(myInstance, i, j, channels.data(), static_cast<int32_t>(channels.size()), &filled);
                                break;
                            }
                            default:
                                break;
                            }
                        }
                        
                    }
                }
            }
        }

        if (TPInstanceStartFrameAtTime(myInstance, 1000, 1000 * 1000) == TPResultSuccess)
        {
            myInFrame = true;
            myLastFloatValue += 1.0/(60.0 * 8.0);
        }
        else
        {
            myLastStreamValue = 1.0;
            color[0] = 1.0f;
            color[1] = color[2] = 0.2f;
        }
    }

    float intensity = (myLastStreamValue + 1.0) / 2.0;
    myRenderer.setBackgroundColor(color[0] * intensity, color[1] * intensity, color[2] * intensity);
    myRenderer.render();
}

void DocumentWindow::cancelFrame()
{
    if (myInFrame)
    {
        TPResult result = TPInstanceCancelFrame(myInstance);
        if (result != TPResultSuccess)
        {
            // TODO: post it
        }
    }
}
