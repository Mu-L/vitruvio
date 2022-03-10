#include "TextureDecoding.h"

#include <string>

namespace
{
struct FTextureSettings
{
	bool SRGB;
	TextureCompressionSettings Compression;
};

FTextureSettings GetTextureSettings(const FString& Key, EPixelFormat PixelFormat)
{
	if (Key == L"normalMap")
	{
		return {false, TC_Normalmap};
	}
	if (Key == L"roughnessMap" || Key == L"metallicMap")
	{
		return {false, TC_Masks};
	}
	bool IsGrayscale = PixelFormat == EPixelFormat::PF_G8 || PixelFormat == EPixelFormat::PF_G16 || EPixelFormat::PF_R32_FLOAT;
	return {!IsGrayscale, TC_Default};
}
} // namespace

namespace Vitruvio
{

FTextureMetadata ParseTextureMetadata(const prt::AttributeMap* TextureMetadata)
{
	Vitruvio::FTextureMetadata Result;
	Result.Width = TextureMetadata->getInt(L"width");
	Result.Height = TextureMetadata->getInt(L"height");
	Result.BytesPerBand = 0;
	Result.Bands = 0;

	FString Format(TextureMetadata->getString(L"format"));
	if (Format == TEXT("GREY8"))
	{
		Result.BytesPerBand = 1;
		Result.Bands = 1;
		Result.PixelFormat = EPixelFormat::PF_G8;
	}
	else if (Format == TEXT("GREY16"))
	{
		Result.BytesPerBand = 2;
		Result.Bands = 1;
		Result.PixelFormat = EPixelFormat::PF_G16;
	}
	else if (Format == TEXT("FLOAT32"))
	{
		Result.BytesPerBand = 4;
		Result.Bands = 1;
		Result.PixelFormat = EPixelFormat::PF_R32_FLOAT;
	}
	else if (Format == TEXT("RGB8"))
	{
		Result.BytesPerBand = 1;
		Result.Bands = 3;
		Result.PixelFormat = EPixelFormat::PF_R8G8B8A8;
	}
	else if (Format == TEXT("RGBA8"))
	{
		Result.BytesPerBand = 1;
		Result.Bands = 4;
		Result.PixelFormat = EPixelFormat::PF_R8G8B8A8;
	}
	else
	{
		Result.PixelFormat = EPixelFormat::PF_Unknown;
	}

	return Result;
}

EPixelFormat GetDefaultPixelFormat(EPixelFormat PixelFormat)
{
	switch (PixelFormat)
	{
	case PF_G8:
	case PF_R8G8B8A8:
	{
		return EPixelFormat::PF_B8G8R8A8;
	}
	case PF_R32_FLOAT:
	{
		return EPixelFormat::PF_FloatRGBA;
	}
	case PF_G16:
	{
		return EPixelFormat::PF_A16B16G16R16;
	}
	default:
	{
		return EPixelFormat::PF_Unknown;
	}
	}
}

FTextureData DecodeTexture(UObject* Outer, const FString& Key, const FString& Path, const FTextureMetadata& TextureMetadata,
						   std::unique_ptr<uint8_t[]> Buffer, size_t BufferSize)
{
	EPixelFormat PixelFormat = TextureMetadata.PixelFormat;

	const size_t BytesPerBand = FMath::Min<size_t>(2, TextureMetadata.BytesPerBand);
	const bool bIsColor = (TextureMetadata.Bands >= 3);

	PixelFormat = GetDefaultPixelFormat(TextureMetadata.PixelFormat);

	size_t NewBufferSize = TextureMetadata.Width * TextureMetadata.Height * 4 * BytesPerBand;
	auto NewBuffer = std::make_unique<uint8_t[]>(NewBufferSize);

	for (int Y = 0; Y < TextureMetadata.Height; ++Y)
	{
		for (int X = 0; X < TextureMetadata.Width; ++X)
		{
			const int NewOffset = (Y * TextureMetadata.Width + X) * 4 * BytesPerBand;
			// Convert 32 bit float textures to 16 bit
			if (TextureMetadata.PixelFormat == PF_R32_FLOAT)
			{
				const int OldOffset = ((TextureMetadata.Height - Y - 1) * TextureMetadata.Width + X) * TextureMetadata.Bands;
				const float* FloatBuffer = reinterpret_cast<const float*>(Buffer.get());

				FFloat16 CurrVal = FFloat16(FloatBuffer[OldOffset]);
				const uint8_t* FloatAsByteArray = reinterpret_cast<uint8_t*>(&CurrVal);
				for (int B = 0; B < BytesPerBand; ++B)
				{
					NewBuffer[NewOffset + 0 * BytesPerBand + B] = FloatAsByteArray[B];
					NewBuffer[NewOffset + 1 * BytesPerBand + B] = FloatAsByteArray[B];
					NewBuffer[NewOffset + 2 * BytesPerBand + B] = FloatAsByteArray[B];
					NewBuffer[NewOffset + 3 * BytesPerBand + B] = 0;
				}
			}
			else
			{
				// Workaround: Also convert grayscale images to rgba, since texture params don't automatically update their sample method
				const int OldOffset = ((TextureMetadata.Height - Y - 1) * TextureMetadata.Width + X) * TextureMetadata.Bands * BytesPerBand;
				for (int B = 0; B < BytesPerBand; ++B)
				{
					NewBuffer[NewOffset + 0 * BytesPerBand + B] = bIsColor ? Buffer[OldOffset + 2 + B] : Buffer[OldOffset + B];
					NewBuffer[NewOffset + 1 * BytesPerBand + B] = bIsColor ? Buffer[OldOffset + 1 + B] : Buffer[OldOffset + B];
					NewBuffer[NewOffset + 2 * BytesPerBand + B] = bIsColor ? Buffer[OldOffset + 0 + B] : Buffer[OldOffset + B];
					NewBuffer[NewOffset + 3 * BytesPerBand + B] = (TextureMetadata.Bands == 4) ? Buffer[OldOffset + 3 + B] : 0;
				}
			}
		}
	}

	Buffer.reset();
	BufferSize = NewBufferSize;
	Buffer = std::move(NewBuffer);

	const FTextureSettings Settings = GetTextureSettings(Key, PixelFormat);

	const FString TextureBaseName = TEXT("T_") + FPaths::GetBaseFilename(Path);
	const FName TextureName = MakeUniqueObjectName(GetTransientPackage(), UTexture2D::StaticClass(), *TextureBaseName);
	UTexture2D* NewTexture = NewObject<UTexture2D>(GetTransientPackage(), TextureName, RF_Transient | RF_TextExportTransient | RF_DuplicateTransient);
	NewTexture->CompressionSettings = Settings.Compression;
	NewTexture->SRGB = Settings.SRGB;

	FTexturePlatformData* PlatformData = new FTexturePlatformData();
	PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = TextureMetadata.Width;
	PlatformData->SizeY = TextureMetadata.Height;
	PlatformData->PixelFormat = PixelFormat;

	// Allocate first mipmap and upload the pixel data
	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	PlatformData->Mips.Add(Mip);
	Mip->SizeX = TextureMetadata.Width;
	Mip->SizeY = TextureMetadata.Height;
	Mip->BulkData.Lock(LOCK_READ_WRITE);
	void* TextureData = Mip->BulkData.Realloc(CalculateImageBytes(TextureMetadata.Width, TextureMetadata.Height, 0, PixelFormat));
	FMemory::Memcpy(TextureData, Buffer.get(), BufferSize);
	Mip->BulkData.Unlock();

	NewTexture->SetPlatformData(PlatformData);

	NewTexture->UpdateResource();

	const auto TimeStamp = FPlatformFileManager::Get().GetPlatformFile().GetAccessTimeStamp(*Path);
	return {NewTexture, static_cast<uint32>(TextureMetadata.Bands), TimeStamp};
}
} // namespace Vitruvio
