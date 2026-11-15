#include "Rules/DCOnlineRules.h"

namespace DCOnlineRules
{
	TMap<FString, FString> ParseKeyValues(const FString& Text)
	{
		TMap<FString, FString> Out;
		TArray<FString> Lines;
		Text.ParseIntoArrayLines(Lines, true);
		for (const FString& Raw : Lines)
		{
			const FString Line = Raw.TrimStartAndEnd();
			if (Line.IsEmpty() || Line.StartsWith(TEXT(";")) || Line.StartsWith(TEXT("#")) || Line.StartsWith(TEXT("[")))
			{
				continue;
			}
			FString Key, Value;
			if (!Line.Split(TEXT("="), &Key, &Value))
			{
				continue;
			}
			Key = Key.TrimStartAndEnd().ToLower();
			Value = Value.TrimStartAndEnd().TrimQuotes();
			if (!Key.IsEmpty())
			{
				Out.Add(Key, Value);
			}
		}
		return Out;
	}

	bool ReadCredentials(const TMap<FString, FString>& Values, FDCOnlineCredentials& Out, FString& OutMissing)
	{
		// One row per field: the key in the file and where it lands.
		const TPair<const TCHAR*, FString*> Fields[] = {
			{TEXT("productid"), &Out.ProductId},
			{TEXT("sandboxid"), &Out.SandboxId},
			{TEXT("deploymentid"), &Out.DeploymentId},
			{TEXT("clientid"), &Out.ClientId},
			{TEXT("clientsecret"), &Out.ClientSecret},
		};
		TArray<FString> Missing;
		for (const TPair<const TCHAR*, FString*>& Field : Fields)
		{
			const FString* Value = Values.Find(Field.Key);
			*Field.Value = Value ? *Value : FString();
			if (Field.Value->IsEmpty())
			{
				Missing.Add(Field.Key);
			}
		}
		OutMissing = FString::Join(Missing, TEXT(", "));
		return Missing.Num() == 0;
	}

	FString MakeCode(const FString& Alphabet, int32 Length, FRandomStream& Stream)
	{
		FString Code;
		if (Alphabet.IsEmpty())
		{
			return Code;
		}
		for (int32 i = 0; i < Length; ++i)
		{
			Code.AppendChar(Alphabet[Stream.RandRange(0, Alphabet.Len() - 1)]);
		}
		return Code;
	}

	bool NormalizeCode(const FString& Input, const FString& Alphabet, int32 Length, FString& OutCode)
	{
		OutCode.Reset();
		for (const TCHAR C : Input.ToUpper())
		{
			if (C == TEXT(' ') || C == TEXT('-') || C == TEXT('\t'))
			{
				continue;
			}
			int32 Index = INDEX_NONE;
			if (!Alphabet.FindChar(C, Index) || OutCode.Len() >= Length)
			{
				OutCode.Reset();
				return false;
			}
			OutCode.AppendChar(C);
		}
		return OutCode.Len() == Length;
	}

	bool UsesTransport(EDCOnlineState State)
	{
		static const TSet<EDCOnlineState> Online = {EDCOnlineState::Hosting, EDCOnlineState::Joining};
		return Online.Contains(State);
	}
}
