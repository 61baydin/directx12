//
//	DirectX12 > Serbest Odev 1	
//
//	61baydin

#include <windows.h>
#include <d3d12.h>
#include "d3dx12.h"
#include <dxgi1_4.h>
#include <D3Dcompiler.h>
#include <DirectXMath.h>
#include <string>
#include <wrl.h>
#include "resource.h"


using namespace DirectX;
using Microsoft::WRL::ComPtr;

struct Vertex
{
	XMFLOAT3 position;
	//XMFLOAT4 color;
};

struct SceneConstantBuffer_ChangesEveryFrame
{
	XMMATRIX mWorld;
	XMFLOAT4 color;
};

struct SceneConstantBuffer_NeverChanges
{
	XMMATRIX mView;
	XMMATRIX mProjection;
};

XMMATRIX g_World;
XMMATRIX g_View;
XMMATRIX g_Projection;

HWND m_hwnd					= NULL;
UINT m_width				= 1280;
UINT m_height				= 720;
UINT m_rtvDescriptorSize	= 0;
bool m_useWarpDevice		= false;	// Adapter info.
float rotation				= 0.0;
const UINT FrameCount		= 2;

// Pipeline objects.
D3D12_VIEWPORT						m_viewport;
D3D12_RECT							m_scissorRect;
ComPtr<IDXGISwapChain3>				m_swapChain;
ComPtr<ID3D12Device>				m_device;
ComPtr<ID3D12Resource>				m_renderTargets[FrameCount];
ComPtr<ID3D12CommandAllocator>		m_commandAllocator;
ComPtr<ID3D12CommandQueue>			m_commandQueue;
ComPtr<ID3D12RootSignature>			m_rootSignature;
ComPtr<ID3D12DescriptorHeap>		m_rtvHeap;
ComPtr<ID3D12PipelineState>			m_pipelineState;
ComPtr<ID3D12GraphicsCommandList>	m_commandList;

// Depth/Stencil
ComPtr<ID3D12DescriptorHeap>		m_dsvHeap;
ComPtr<ID3D12Resource>				m_depthStencil;

// App resources.
ComPtr<ID3D12Resource>				m_vertexBuffer;
D3D12_VERTEX_BUFFER_VIEW			m_vertexBufferView;

ComPtr<ID3D12Resource>					m_constantBuffer_ChangesEveryFrame;
SceneConstantBuffer_ChangesEveryFrame	m_constantBufferData_ChangesEveryFrame;
UINT8*									m_pCbvDataBegin_ChangesEveryFrame = NULL;

ComPtr<ID3D12Resource>				m_constantBuffer_NeverChanges;
SceneConstantBuffer_NeverChanges	m_constantBufferData_NeverChanges;
UINT8*								m_pCbvDataBegin_NeverChanges = NULL;

// Synchronization objects.
UINT								m_frameIndex;
HANDLE								m_fenceEvent;
ComPtr<ID3D12Fence>					m_fence;
UINT64								m_fenceValue;

void OnInit();
void OnUpdate();
void OnRender();

void OnDestroy();
void PopulateCommandList();
void WaitForPreviousFrame();
void ThrowIfFailed(HRESULT hr);
void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter);
LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

void OnInit()
{
	#if defined(_DEBUG)
		// Enable the D3D12 debug layer.
		{
			ComPtr<ID3D12Debug> debugController;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController))))
			{
				debugController->EnableDebugLayer();
			}
		}
	#endif

	ComPtr<IDXGIFactory4> factory;
	ThrowIfFailed(CreateDXGIFactory1(IID_PPV_ARGS(&factory)));

	if (m_useWarpDevice)
	{
		ComPtr<IDXGIAdapter> warpAdapter;
		ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

		ThrowIfFailed(D3D12CreateDevice(
			warpAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}
	else
	{
		ComPtr<IDXGIAdapter1> hardwareAdapter;
		GetHardwareAdapter(factory.Get(), &hardwareAdapter);

		ThrowIfFailed(D3D12CreateDevice(
			hardwareAdapter.Get(),
			D3D_FEATURE_LEVEL_11_0,
			IID_PPV_ARGS(&m_device)
		));
	}

	// Describe and create the command queue.
	D3D12_COMMAND_QUEUE_DESC queueDesc	= {};
	queueDesc.Flags						= D3D12_COMMAND_QUEUE_FLAG_NONE;
	queueDesc.Type						= D3D12_COMMAND_LIST_TYPE_DIRECT;

	ThrowIfFailed(m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

	// Describe and create the swap chain.
	DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
	swapChainDesc.BufferCount			= FrameCount;
	swapChainDesc.Width					= m_width;
	swapChainDesc.Height				= m_height;
	swapChainDesc.Format				= DXGI_FORMAT_R8G8B8A8_UNORM;
	swapChainDesc.BufferUsage			= DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapChainDesc.SwapEffect			= DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapChainDesc.SampleDesc.Count		= 1;

	ComPtr<IDXGISwapChain1> swapChain;
	ThrowIfFailed(factory->CreateSwapChainForHwnd(
		m_commandQueue.Get(),			// Swap chain needs the queue so that it can force a flush on it.
		m_hwnd,
		&swapChainDesc,
		nullptr,
		nullptr,
		&swapChain
	));

	// This sample does not support fullscreen transitions.
	ThrowIfFailed(factory->MakeWindowAssociation(m_hwnd, DXGI_MWA_NO_ALT_ENTER));

	ThrowIfFailed(swapChain.As(&m_swapChain));
	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

	// Create descriptor heaps.
	{
		// Describe and create a render target view (RTV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc	= {};
		rtvHeapDesc.NumDescriptors				= FrameCount;
		rtvHeapDesc.Type						= D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		rtvHeapDesc.Flags						= D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

		m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

		// Describe and create a depth stencil view (DSV) descriptor heap.
		D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc	= {};
		dsvHeapDesc.NumDescriptors				= 1;
		dsvHeapDesc.Type						= D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		dsvHeapDesc.Flags						= D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
		ThrowIfFailed(m_device->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(&m_dsvHeap)));
	}

	// Create frame resources.
	{
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

		// Create a RTV for each frame.
		for (UINT n = 0; n < FrameCount; n++)
		{
			ThrowIfFailed(m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
			m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr, rtvHandle);
			rtvHandle.Offset(1, m_rtvDescriptorSize);
		}
	}

	ThrowIfFailed(m_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));

	// Create the command list.
	ThrowIfFailed(m_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(), m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

	// Command lists are created in the recording state, but there is nothing to record yet. The main loop expects it to be closed, so close it now.
	ThrowIfFailed(m_commandList->Close());

	// Create synchronization objects and wait until assets have been uploaded to the GPU.
	{
		ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)));
		m_fenceValue = 1;

		// Create an event handle to use for frame synchronization.
		m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
		if (m_fenceEvent == nullptr)
		{
			ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
		}

		// Wait for the command list to execute; we are reusing the same command list in our main loop but for now, we just want to wait for setup to complete before continuing.
		WaitForPreviousFrame();
	}

	// Graphics root signature.
	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE featureData = {};

		// This is the highest version the sample supports. If CheckFeatureSupport succeeds, the HighestVersion returned will not be greater than this.
		featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;

		if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &featureData, sizeof(featureData))))
		{
			featureData.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}

		CD3DX12_ROOT_PARAMETER1 rootParameters[2];
		rootParameters[0].InitAsConstantBufferView(6, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
		rootParameters[1].InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);

		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
		rootSignatureDesc.Init_1_1(_countof(rootParameters), rootParameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		ComPtr<ID3DBlob> signature;
		ComPtr<ID3DBlob> error;
		ThrowIfFailed(D3DX12SerializeVersionedRootSignature(&rootSignatureDesc, featureData.HighestVersion, &signature, &error));
		ThrowIfFailed(m_device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&m_rootSignature)));
	}

	// Create the pipeline state, which includes compiling and loading shaders.
	{
		ComPtr<ID3DBlob> vertexShader;
		ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
		// Enable better shader debugging with the graphics debugging tools.
		UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
		UINT compileFlags = 0;
#endif

		ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "VSMain", "vs_5_0", compileFlags, 0, &vertexShader, nullptr));
		ThrowIfFailed(D3DCompileFromFile(L"shaders.hlsl", nullptr, nullptr, "PSMain", "ps_5_0", compileFlags, 0, &pixelShader, nullptr));

		// Define the vertex input layout.
		D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
		{
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0,  0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			//{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
		};

		// Describe and create the graphics pipeline state object (PSO).
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc	= {};
		psoDesc.InputLayout							= { inputElementDescs, _countof(inputElementDescs) };
		psoDesc.pRootSignature						= m_rootSignature.Get();
		psoDesc.VS									= CD3DX12_SHADER_BYTECODE(vertexShader.Get());
		psoDesc.PS									= CD3DX12_SHADER_BYTECODE(pixelShader.Get());
		psoDesc.RasterizerState						= CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
		psoDesc.BlendState							= CD3DX12_BLEND_DESC(D3D12_DEFAULT);
		psoDesc.DepthStencilState					= CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
		psoDesc.SampleMask							= UINT_MAX;
		psoDesc.PrimitiveTopologyType				= D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		psoDesc.NumRenderTargets					= 1;
		psoDesc.RTVFormats[0]						= DXGI_FORMAT_R8G8B8A8_UNORM;
		psoDesc.DSVFormat							= DXGI_FORMAT_D32_FLOAT;
		psoDesc.SampleDesc.Count					= 1;
		psoDesc.RasterizerState.CullMode			= D3D12_CULL_MODE_BACK;		// Backface Culling yapmasý için : D3D12_CULL_MODE_BACK 
		psoDesc.RasterizerState.FillMode			= D3D12_FILL_MODE_SOLID;	// Sadece kenarlarý render etmek için : D3D12_FILL_MODE_WIREFRAME 

		ThrowIfFailed(m_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
	}

	// Create the depth stencil view.
	{
		D3D12_DEPTH_STENCIL_VIEW_DESC depthStencilDesc	= {};
		depthStencilDesc.Format							= DXGI_FORMAT_D32_FLOAT;
		depthStencilDesc.ViewDimension					= D3D12_DSV_DIMENSION_TEXTURE2D;
		depthStencilDesc.Flags							= D3D12_DSV_FLAG_NONE;

		D3D12_CLEAR_VALUE depthOptimizedClearValue		= {};
		depthOptimizedClearValue.Format					= DXGI_FORMAT_D32_FLOAT;
		depthOptimizedClearValue.DepthStencil.Depth		= 1.0f;
		depthOptimizedClearValue.DepthStencil.Stencil	= 0;

		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT, m_width, m_height, 1, 0, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			&depthOptimizedClearValue,
			IID_PPV_ARGS(&m_depthStencil)
		));

		m_device->CreateDepthStencilView(m_depthStencil.Get(), &depthStencilDesc, m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	}

	// Create the vertex buffer.
	{
		// Define the geometry for a triangle.
		Vertex triangleVertices[] =
		{
			{ XMFLOAT3( 1.0f,  1.0f,  0.0f)},
			{ XMFLOAT3( 1.0f, -1.0f,  0.0f)},
			{ XMFLOAT3(-1.0f, -1.0f,  0.0f)},

			{ XMFLOAT3( 1.0f,  1.0f,  0.0f)},
			{ XMFLOAT3(-1.0f, -1.0f,  0.0f)},
			{ XMFLOAT3(-1.0f,  1.0f,  0.0f)}
		};

		const UINT vertexBufferSize = sizeof(triangleVertices);

		// Note: using upload heaps to transfer static data like vert buffers is not recommended. Every time the GPU needs it, the upload heap will be marshalled over. 
		// Please read up on Default Heap usage. An upload heap is used here for code simplicity and because there are very few verts to actually transfer.
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_vertexBuffer)));

		// Copy the triangle data to the vertex buffer.
		UINT8* pVertexDataBegin;
		CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_vertexBuffer->Map(0, &readRange, reinterpret_cast<void**>(&pVertexDataBegin)));
		memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
		m_vertexBuffer->Unmap(0, nullptr);

		// Initialize the vertex buffer view.
		m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
		m_vertexBufferView.StrideInBytes  = sizeof(Vertex);
		m_vertexBufferView.SizeInBytes    = vertexBufferSize;
	}

	// Create the constant buffer _ChangesEveryFrame.
	{
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(1024 * 64),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_constantBuffer_ChangesEveryFrame)));

		// Initialize and map the constant buffers. We don't unmap this until the app closes. Keeping things mapped for the lifetime of the resource is okay.
		ZeroMemory(&m_constantBufferData_ChangesEveryFrame, sizeof(m_constantBufferData_ChangesEveryFrame));

		CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_constantBuffer_ChangesEveryFrame->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin_ChangesEveryFrame)));
	}

	// Create the constant buffer _NeverChanges.
	{
		ThrowIfFailed(m_device->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
			D3D12_HEAP_FLAG_NONE,
			&CD3DX12_RESOURCE_DESC::Buffer(1024 * 64),
			D3D12_RESOURCE_STATE_GENERIC_READ,
			nullptr,
			IID_PPV_ARGS(&m_constantBuffer_NeverChanges)));

		// Initialize and map the constant buffers. We don't unmap this until the app closes. Keeping things mapped for the lifetime of the resource is okay.
		ZeroMemory(&m_constantBufferData_NeverChanges, sizeof(m_constantBufferData_NeverChanges));

		CD3DX12_RANGE readRange(0, 0);		// We do not intend to read from this resource on the CPU.
		ThrowIfFailed(m_constantBuffer_NeverChanges->Map(0, &readRange, reinterpret_cast<void**>(&m_pCbvDataBegin_NeverChanges)));
	}

	// Initialize the world matrix
	g_World = XMMatrixIdentity();

	// Initialize the view matrix
	XMVECTOR Eye = XMVectorSet( 0.0f, 4.0f, -6.0f, 0.0f );
	XMVECTOR At  = XMVectorSet( 0.0f, 0.0f,  1.0f, 0.0f );
	XMVECTOR Up  = XMVectorSet( 0.0f, 1.0f,  0.0f, 0.0f );
	g_View		 = XMMatrixLookAtLH(Eye, At, Up);

	// Initialize the projection matrix
	g_Projection = XMMatrixPerspectiveFovLH(XM_PIDIV4, 1280 / (FLOAT)720, 0.01f, 100.0f);

	m_constantBufferData_ChangesEveryFrame.mWorld	= XMMatrixTranspose(g_World);
	m_constantBufferData_ChangesEveryFrame.color    = XMFLOAT4(1, 0, 0, 1);
	memcpy(m_pCbvDataBegin_ChangesEveryFrame, &m_constantBufferData_ChangesEveryFrame, sizeof(m_constantBufferData_ChangesEveryFrame));


	m_constantBufferData_NeverChanges.mView			= XMMatrixTranspose(g_View);
	m_constantBufferData_NeverChanges.mProjection	= XMMatrixTranspose(g_Projection);

	memcpy(m_pCbvDataBegin_NeverChanges, &m_constantBufferData_NeverChanges, sizeof(m_constantBufferData_NeverChanges));

}

// Update frame-based values.
void OnUpdate()
{
	rotation += 0.01;

	//on
	XMMATRIX mRotate = XMMatrixRotationY(rotation);
	XMMATRIX mTranslate = XMMatrixTranslation(0.0f, 0.0f, -1.0f);
	g_World = mTranslate*mRotate;
	m_constantBufferData_ChangesEveryFrame.mWorld = XMMatrixTranspose(g_World);
	memcpy(m_pCbvDataBegin_ChangesEveryFrame, &m_constantBufferData_ChangesEveryFrame, sizeof(m_constantBufferData_ChangesEveryFrame));
	m_constantBufferData_ChangesEveryFrame.color = XMFLOAT4(1, 0, 0, 1);

	//arka
	XMMATRIX mRotate0 = XMMatrixRotationY(XM_PI);
	mTranslate = XMMatrixTranslation(0.0f, 0.0f, 1.0f);
	g_World = mRotate0*mTranslate*mRotate;
	m_constantBufferData_ChangesEveryFrame.mWorld = XMMatrixTranspose(g_World);
	memcpy(m_pCbvDataBegin_ChangesEveryFrame+256, &m_constantBufferData_ChangesEveryFrame, sizeof(m_constantBufferData_ChangesEveryFrame));
	m_constantBufferData_ChangesEveryFrame.color = XMFLOAT4(0, 1, 0, 1);

	//sag
	XMMATRIX mRotate1 = XMMatrixRotationY(-XM_PI/2);
	mTranslate = XMMatrixTranslation(1.0f, 0.0f, 0.0f);
	g_World = mRotate1*mTranslate*mRotate;
	m_constantBufferData_ChangesEveryFrame.mWorld = XMMatrixTranspose(g_World);
	memcpy(m_pCbvDataBegin_ChangesEveryFrame + 2*256, &m_constantBufferData_ChangesEveryFrame, sizeof(m_constantBufferData_ChangesEveryFrame));
	m_constantBufferData_ChangesEveryFrame.color = XMFLOAT4(0, 0, 1, 1);

	//sol
	XMMATRIX mRotate2 = XMMatrixRotationY(XM_PI / 2);
	mTranslate = XMMatrixTranslation(-1.0f, 0.0f, 0.0f);
	g_World = mRotate2*mTranslate*mRotate;
	m_constantBufferData_ChangesEveryFrame.mWorld = XMMatrixTranspose(g_World);
	memcpy(m_pCbvDataBegin_ChangesEveryFrame + 3 * 256, &m_constantBufferData_ChangesEveryFrame, sizeof(m_constantBufferData_ChangesEveryFrame));
	m_constantBufferData_ChangesEveryFrame.color = XMFLOAT4(1, 0, 1, 1);

	//ust
	XMMATRIX mRotate3 = XMMatrixRotationX(XM_PI/2);
	mTranslate = XMMatrixTranslation(0.0f, 1.0f, 0.0f);
	g_World = mRotate3*mTranslate*mRotate;
	m_constantBufferData_ChangesEveryFrame.mWorld = XMMatrixTranspose(g_World);
	memcpy(m_pCbvDataBegin_ChangesEveryFrame + 4 * 256, &m_constantBufferData_ChangesEveryFrame, sizeof(m_constantBufferData_ChangesEveryFrame));
	m_constantBufferData_ChangesEveryFrame.color = XMFLOAT4(125, 100, 24, 1);

	//alt
	XMMATRIX mRotate4 = XMMatrixRotationX(-XM_PI / 2);
	mTranslate = XMMatrixTranslation(0.0f, -1.0f, 0.0f);
	g_World = mRotate4*mTranslate*mRotate;
	m_constantBufferData_ChangesEveryFrame.mWorld = XMMatrixTranspose(g_World);
	memcpy(m_pCbvDataBegin_ChangesEveryFrame + 5 * 256, &m_constantBufferData_ChangesEveryFrame, sizeof(m_constantBufferData_ChangesEveryFrame));
	m_constantBufferData_ChangesEveryFrame.color = XMFLOAT4(1, 1, 1, 1);
}

// Render the scene.
void OnRender()
{
	// Record all the commands we need to render the scene into the command list.
	PopulateCommandList();

	// Execute the command list.
	ID3D12CommandList* ppCommandLists[] = { m_commandList.Get() };
	m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

	// Present the frame.
	ThrowIfFailed(m_swapChain->Present(1, 0));

	WaitForPreviousFrame();
}

// Fill the command list with all the render commands and dependent state.
void PopulateCommandList()
{
	// Command list allocators can only be reset when the associated command lists 
	// have finished execution on the GPU; apps should use fences to determine GPU execution progress.
	ThrowIfFailed(m_commandAllocator->Reset());

	// However, when ExecuteCommandList() is called on a particular command list, 
	// that command list can then be reset at any time and must be before re-recording.
	ThrowIfFailed(m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

	// Set necessary state.
	m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());

	m_viewport.Width		= static_cast<float>(m_width);
	m_viewport.Height		= static_cast<float>(m_height);
	m_viewport.MaxDepth		= 1.0f;

	m_scissorRect.right		= static_cast<float>(m_width);
	m_scissorRect.bottom	= static_cast<float>(m_height);

	m_commandList->RSSetViewports(1, &m_viewport);
	m_commandList->RSSetScissorRects(1, &m_scissorRect);

	// Indicate that the back buffer will be used as a render target.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex, m_rtvDescriptorSize);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsvHandle(m_dsvHeap->GetCPUDescriptorHandleForHeapStart());
	m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// Record commands.
	const float clearColor[] = { 0.0f, 0.2f, 0.4f, 1.0f };
	m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	m_commandList->ClearDepthStencilView(m_dsvHeap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer_ChangesEveryFrame->GetGPUVirtualAddress()); //on
	m_commandList->SetGraphicsRootConstantBufferView(1, m_constantBuffer_NeverChanges->GetGPUVirtualAddress());
	m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
	m_commandList->DrawInstanced(6, 1, 0, 0);

	m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer_ChangesEveryFrame->GetGPUVirtualAddress()+256); //arka
	m_commandList->DrawInstanced(6, 1, 0, 0);

	m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer_ChangesEveryFrame->GetGPUVirtualAddress() + 2*256); //sag
	m_commandList->DrawInstanced(6, 1, 0, 0);

	m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer_ChangesEveryFrame->GetGPUVirtualAddress() + 3 * 256); //sol
	m_commandList->DrawInstanced(6, 1, 0, 0);

	m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer_ChangesEveryFrame->GetGPUVirtualAddress() + 4 * 256); //ust
	m_commandList->DrawInstanced(6, 1, 0, 0);

	m_commandList->SetGraphicsRootConstantBufferView(0, m_constantBuffer_ChangesEveryFrame->GetGPUVirtualAddress() + 5 * 256); //alt
	m_commandList->DrawInstanced(6, 1, 0, 0);

	// Indicate that the back buffer will now be used to present.
	m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	ThrowIfFailed(m_commandList->Close());
}

void WaitForPreviousFrame()
{
	// Signal and increment the fence value.
	const UINT64 fence = m_fenceValue;
	ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
	m_fenceValue++;

	// Wait until the previous frame is finished.
	if (m_fence->GetCompletedValue() < fence)
	{
		ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
		WaitForSingleObject(m_fenceEvent, INFINITE);
	}

	m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void OnDestroy()
{
	// Ensure that the GPU is no longer referencing resources that are about to be cleaned up by the destructor.
	WaitForPreviousFrame();

	CloseHandle(m_fenceEvent);
}

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
	// Initialize the window class.
	WNDCLASSEX windowClass		= { 0 };
	windowClass.cbSize			= sizeof(WNDCLASSEX);
	windowClass.style			= CS_HREDRAW | CS_VREDRAW;
	windowClass.lpfnWndProc		= WindowProc;
	windowClass.hInstance		= hInstance;
	windowClass.hCursor			= LoadCursor(NULL, IDC_ARROW);
	windowClass.lpszClassName	= L"D3D12HelloTriangle";
	windowClass.hIcon			= LoadIcon(hInstance, (LPCTSTR)IDI_TUTORIAL1);
	RegisterClassEx(&windowClass);

	RECT windowRect = { 0, 0, (LONG)m_width, (LONG)m_height };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	// Create the window and store a handle to it.
	m_hwnd = CreateWindow(
		windowClass.lpszClassName,
		L"DirectX12 > Serbest Odev 1",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr,									// We have no parent window.
		nullptr,									// We aren't using menus.
		hInstance,
		NULL);

	// Initialize the sample. OnInit is defined in each child-implementation of sample.
	OnInit();

	ShowWindow(m_hwnd, nCmdShow);

	// Main sample loop.
	MSG msg = {};
	while (msg.message != WM_QUIT)
	{
		// Process any messages in the queue.
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}

	OnDestroy();

	// Return this part of the WM_QUIT message to Windows.
	return static_cast<char>(msg.wParam);
}

LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
		case WM_CREATE:				
			return 0;

		case WM_KEYDOWN:			
			return 0;

		case WM_KEYUP:				
			return 0;

		case WM_PAINT:
			OnUpdate();
			OnRender();
			return 0;

		case WM_DESTROY:
			PostQuitMessage(0);
			return 0;
	}

	// Handle any messages the switch statement didn't.
	return DefWindowProc(hWnd, message, wParam, lParam);
}

_Use_decl_annotations_
void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter)
{
	ComPtr<IDXGIAdapter1> adapter;
	*ppAdapter = nullptr;

	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex)
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Don't select the Basic Render Driver adapter. If you want a software adapter, pass in "/warp" on the command line.
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}
	*ppAdapter = adapter.Detach();
}

void ThrowIfFailed(HRESULT hr)
{
	if (FAILED(hr))
	{
		throw std::exception();
	}
}
