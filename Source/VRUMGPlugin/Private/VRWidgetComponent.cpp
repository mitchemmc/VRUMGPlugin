// Fill out your copyright notice in the Description page of Project Settings.

#include "VRUMGPluginPrivatePCH.h"
#include "VRWidgetComponent.h"
#include "SlateCore.h"
#include "HittestGrid.h"
#include "Slate/WidgetRenderer.h"
#include "Events.h"



DECLARE_CYCLE_STAT(TEXT("3DHitTesting"), STAT_Slate3DHitTesting, STATGROUP_Slate);

/**
* The hit tester used by VR Widget Component objects.
*/
class FWidgetVRHitTester : public ICustomHitTestPath
{
public:
	FWidgetVRHitTester(UWorld* InWorld)
		: World(InWorld)
		, CachedFrame(-1), UseCustomHit(false)
	{}

	// ICustomHitTestPath implementation
	virtual TArray<FWidgetAndPointer> GetBubblePathAndVirtualCursors(const FGeometry& InGeometry, FVector2D DesktopSpaceCoordinate, bool bIgnoreEnabledStatus) const override
	{
		SCOPE_CYCLE_COUNTER(STAT_Slate3DHitTesting);

		if (World.IsValid() && ensure(World->IsGameWorld()))
		{
			UWorld* SafeWorld = World.Get();
			if (SafeWorld)
			{
				ULocalPlayer* const TargetPlayer = GEngine->GetLocalPlayerFromControllerId(SafeWorld, 0);

				if (TargetPlayer && TargetPlayer->PlayerController)
				{
					if (UPrimitiveComponent* HitComponent = GetHitResultAtScreenPositionAndCache(TargetPlayer->PlayerController, InGeometry.AbsoluteToLocal(DesktopSpaceCoordinate)))
					{
						if (UWidgetComponent* WidgetComponent = Cast<UWidgetComponent>(HitComponent))
						{
							// Get the "forward" vector based on the current rotation system.
							const FVector ForwardVector = WidgetComponent->IsUsingLegacyRotation() ? WidgetComponent->GetUpVector() : WidgetComponent->GetForwardVector();

							// Make sure the player is interacting with the front of the widget
							if (FVector::DotProduct(ForwardVector, CachedHitResult.ImpactPoint - CachedHitResult.TraceStart) < 0.f)
							{
								// Make sure the player is close enough to the widget to interact with it
								if (FVector::DistSquared(CachedHitResult.TraceStart, WidgetComponent->GetComponentLocation()) <= FMath::Square(WidgetComponent->GetMaxInteractionDistance()))
								{
									return WidgetComponent->GetHitWidgetPath(CachedHitResult.Location, bIgnoreEnabledStatus);
								}
							}
						}
					}
				}
			}
		}

		return TArray<FWidgetAndPointer>();
	}

	virtual void ArrangeChildren(FArrangedChildren& ArrangedChildren) const override
	{
		for (TWeakObjectPtr<UWidgetComponent> Component : RegisteredComponents)
		{
			UWidgetComponent* WidgetComponent = Component.Get();
			// Check if visible;
			if (WidgetComponent && WidgetComponent->GetSlateWidget().IsValid())
			{
				FGeometry WidgetGeom;

				ArrangedChildren.AddWidget(FArrangedWidget(WidgetComponent->GetSlateWidget().ToSharedRef(), WidgetGeom.MakeChild(WidgetComponent->GetDrawSize(), FSlateLayoutTransform())));
			}
		}
	}

	virtual TSharedPtr<struct FVirtualPointerPosition> TranslateMouseCoordinateFor3DChild(const TSharedRef<SWidget>& ChildWidget, const FGeometry& ViewportGeometry, const FVector2D& ScreenSpaceMouseCoordinate, const FVector2D& LastScreenSpaceMouseCoordinate) const override
	{
		if (World.IsValid() && ensure(World->IsGameWorld()))
		{
			ULocalPlayer* const TargetPlayer = GEngine->GetLocalPlayerFromControllerId(World.Get(), 0);
			if (TargetPlayer && TargetPlayer->PlayerController)
			{
				FVector2D LocalMouseCoordinate = ViewportGeometry.AbsoluteToLocal(ScreenSpaceMouseCoordinate);

				// Check for a hit against any widget components in the world
				for (TWeakObjectPtr<UWidgetComponent> Component : RegisteredComponents)
				{
					UWidgetComponent* WidgetComponent = Component.Get();
					// Check if visible;
					if (WidgetComponent && WidgetComponent->GetSlateWidget() == ChildWidget)
					{
						if (UPrimitiveComponent* HitComponent = GetHitResultAtScreenPositionAndCache(TargetPlayer->PlayerController, LocalMouseCoordinate))
						{
							if (WidgetComponent == HitComponent)
							{
								TSharedPtr<FVirtualPointerPosition> VirtualCursorPos = MakeShareable(new FVirtualPointerPosition);

								FVector2D LocalHitLocation;
								WidgetComponent->GetLocalHitLocation(CachedHitResult.Location, LocalHitLocation);

								VirtualCursorPos->CurrentCursorPosition = LocalHitLocation;
								VirtualCursorPos->LastCursorPosition = LocalHitLocation;

								return VirtualCursorPos;
							}
						}
					}
				}
			}
		}

		return nullptr;
	}
	// End ICustomHitTestPath

	UPrimitiveComponent* GetHitResultAtScreenPositionAndCache(APlayerController* PlayerController, FVector2D ScreenPosition) const
	{
		UPrimitiveComponent* HitComponent = nullptr;

		if (UseCustomHit)
		{
			return CachedHitResult.Component.Get();
		}

		if (GFrameNumber != CachedFrame || CachedScreenPosition != ScreenPosition)
		{
			CachedFrame = GFrameNumber;
			CachedScreenPosition = ScreenPosition;

			if (PlayerController)
			{
				if (PlayerController->GetHitResultAtScreenPosition(ScreenPosition, ECC_Visibility, true, CachedHitResult))
				{
					return CachedHitResult.Component.Get();
				}
			}
		}
		else
		{
			return CachedHitResult.Component.Get();
		}

		return nullptr;
	}

	void SetCustomHit(FHitResult hit) 
	{
		if (!UseCustomHit) 
		{
			UseCustomHit = true;
		}
		CachedHitResult = hit;
	}

	void RegisterWidgetComponent(UWidgetComponent* InComponent)
	{
		RegisteredComponents.AddUnique(InComponent);
	}

	void UnregisterWidgetComponent(UWidgetComponent* InComponent)
	{
		RegisteredComponents.RemoveSingleSwap(InComponent);
	}

	uint32 GetNumRegisteredComponents() const { return RegisteredComponents.Num(); }

	UWorld* GetWorld() const { return World.Get(); }

private:
	TArray< TWeakObjectPtr<UWidgetComponent> > RegisteredComponents;
	TWeakObjectPtr<UWorld> World;

	mutable int64 CachedFrame;
	mutable FVector2D CachedScreenPosition;
	mutable FHitResult CachedHitResult;
	mutable bool UseCustomHit;
};

void UVRWidgetComponent::OnRegister()
{
	Super::Super::OnRegister();

#if !UE_SERVER
	if (!IsRunningDedicatedServer())
	{
		if (Space != EWidgetSpace::Screen)
		{
			if (GetWorld()->IsGameWorld())
			{
				TSharedPtr<SViewport> GameViewportWidget = GEngine->GetGameViewportWidget();

				if (GameViewportWidget.IsValid())
				{
					TSharedPtr<ICustomHitTestPath> CustomHitTestPath = GameViewportWidget->GetCustomHitTestPath();
					if (!CustomHitTestPath.IsValid())
					{
						CustomHitTestPath = MakeShareable(new FWidgetVRHitTester(GetWorld()));
						GameViewportWidget->SetCustomHitTestPath(CustomHitTestPath);
					}

					TSharedPtr<FWidgetVRHitTester> WidgetVRHitTester = StaticCastSharedPtr<FWidgetVRHitTester>(CustomHitTestPath);
					if (WidgetVRHitTester->GetWorld() == GetWorld())
					{
						WidgetVRHitTester->RegisterWidgetComponent(this);
						WidgetHitTester = WidgetVRHitTester;
					}
				}
			}

			if (!MaterialInstance)
			{
				UpdateMaterialInstance();
			}
		}

		if (Space != EWidgetSpace::Screen)
		{
			if (!WidgetRenderer.IsValid() && !GUsingNullRHI)
			{
				WidgetRenderer = MakeShareable(new FWidgetRenderer());
			}
		}

		BodySetup = nullptr;

		InitWidget();
	}
#endif // !UE_SERVER

}

void UVRWidgetComponent::OnUnregister()
{
	if (GetWorld()->IsGameWorld())
	{
		TSharedPtr<SViewport> GameViewportWidget = GEngine->GetGameViewportWidget();
		if (GameViewportWidget.IsValid())
		{
			TSharedPtr<ICustomHitTestPath> CustomHitTestPath = GameViewportWidget->GetCustomHitTestPath();
			if (CustomHitTestPath.IsValid())
			{
				TSharedPtr<FWidgetVRHitTester> WidgetHitTestPath = StaticCastSharedPtr<FWidgetVRHitTester>(CustomHitTestPath);

				WidgetHitTestPath->UnregisterWidgetComponent(this);

				if (WidgetHitTestPath->GetNumRegisteredComponents() == 0)
				{
					GameViewportWidget->SetCustomHitTestPath(nullptr);
				}
			}
		}
	}

#if WITH_EDITOR
	if (!GetWorld()->IsGameWorld())
	{
		ReleaseResources();
	}
#endif

	Super::Super::OnUnregister();
}

void UVRWidgetComponent::GetLocalHit(FVector WorldHitLocation, FVector2D& OutLocalHitLocation) 
{
	GetLocalHitLocation(WorldHitLocation, OutLocalHitLocation);
}


void UVRWidgetComponent::SetCustomHit(FHitResult Hit, bool SimulateHover)
{
	if (GetSlateWidget().IsValid()) 
	{
		WidgetHitTester->SetCustomHit(Hit);
	}
	if (SimulateHover)
	{
		TArray<FWidgetAndPointer> ArrangedWidgets = GetHitWidgetPath(Hit.Location, false, 1.0);
		if (HitTestGrid.IsValid())
		{
			TArray<FWidgetAndPointer> WidgetsToRemove;
			for (FWidgetAndPointer& PreviousHoveredWidget : HoveredWidgets)
			{
				if (!ArrangedWidgets.Contains(PreviousHoveredWidget))
				{
					const FPointerEvent pointerEvent = FPointerEvent(0, 0, PreviousHoveredWidget.PointerPosition->CurrentCursorPosition, PreviousHoveredWidget.PointerPosition->LastCursorPosition, false);
					PreviousHoveredWidget.Widget->OnMouseLeave(pointerEvent);
					WidgetsToRemove.Add(PreviousHoveredWidget);
					// Recapture mouse after leaveing widget
					if (GetOwnerPlayer() && GetSlateWidget().IsValid())
					{
						GetOwnerPlayer()->GetSlateOperations().SetUserFocus(GetSlateWidget().ToSharedRef());
						APlayerController* Target = GetOwnerPlayer()->GetPlayerController(GetOwnerPlayer()->GetWorld());
						if (Target != nullptr)
						{
							FInputModeGameOnly InputMode;
							Target->SetInputMode(InputMode);
						}
					}
				}
			}
			for (FWidgetAndPointer& WidgetToRemove : WidgetsToRemove) {
				HoveredWidgets.Remove(WidgetToRemove);
			}
			for (FWidgetAndPointer& ArrangedWidget : ArrangedWidgets)
			{
				if (!HoveredWidgets.Contains(ArrangedWidget))
				{
					HoveredWidgets.Add(ArrangedWidget);
					const FPointerEvent pointerEvent = FPointerEvent(0, 0, ArrangedWidget.PointerPosition->CurrentCursorPosition, ArrangedWidget.PointerPosition->LastCursorPosition, false);
					ArrangedWidget.Widget->OnMouseEnter(ArrangedWidget.Geometry, pointerEvent);
				}
			}
		}
	}
}

void UVRWidgetComponent::Focus(APlayerController* PC)
{
	if (PC)
	{
		ULocalPlayer* const TargetPlayer = Cast< ULocalPlayer >(PC->Player);
		if (TargetPlayer && GetSlateWidget().IsValid())
		{
			TargetPlayer->GetSlateOperations().SetUserFocus(GetSlateWidget().ToSharedRef());
		}
	}
}

void UVRWidgetComponent::EmulateTouchDown(FHitResult Hit, bool PressLeftMouseButton)
{
	TArray<FWidgetAndPointer> ArrangedWidgets = GetHitWidgetPath(Hit.Location, false, 1.0);
	if (HitTestGrid.IsValid())
	{
		for (FWidgetAndPointer& ArrangedWidget : ArrangedWidgets)
		{
			// Create a new touch pointer event and call mouse down on our widget
			const FPointerEvent pointerEvent = FPointerEvent(0, 0, ArrangedWidget.PointerPosition->CurrentCursorPosition, ArrangedWidget.PointerPosition->LastCursorPosition, PressLeftMouseButton);
			FReply reply = ArrangedWidget.Widget->OnMouseButtonDown(ArrangedWidget.Geometry, pointerEvent);

			// Handle the reply from our mouse up to make sure we capture the mouse if we need
			FWidgetPath widgetPath = FWidgetPath(WidgetHitTester->GetBubblePathAndVirtualCursors(FGeometry(), FVector2D(), false)); // Use empty geometry and mouse location as we are going to use our custom hit
			FSlateApplication::Get().ProcessReply(widgetPath, reply, nullptr, &pointerEvent, 0);
		}
	}
}


void UVRWidgetComponent::EmulateTouchUp(FHitResult Hit, bool PressLeftMouseButton)
{
	TArray<FWidgetAndPointer> ArrangedWidgets = GetHitWidgetPath(Hit.Location, false, 1.0);
	if (HitTestGrid.IsValid())
	{
		for (FWidgetAndPointer& ArrangedWidget : ArrangedWidgets)
		{
			// Create a new touch pointer event and call mouse up on our widget
			const FPointerEvent pointerEvent = FPointerEvent(0, 0, ArrangedWidget.PointerPosition->CurrentCursorPosition, ArrangedWidget.PointerPosition->LastCursorPosition, PressLeftMouseButton);
			FReply reply = ArrangedWidget.Widget->OnMouseButtonUp(ArrangedWidget.Geometry, pointerEvent);

			// Handle the reply from our mouse up to make sure we release capuring if we need to
			FWidgetPath widgetPath = FWidgetPath(WidgetHitTester->GetBubblePathAndVirtualCursors(FGeometry(), FVector2D(), false)); // Use empty geometry and mouse location as we are going to use our custom hit
			FSlateApplication::Get().ProcessReply(widgetPath, reply, nullptr, &pointerEvent, 0);
		}
	}
}

void UVRWidgetComponent::EmulateTouchMove(FHitResult Hit, bool PressLeftMouseButton)
{
	TArray<FWidgetAndPointer> ArrangedWidgets = GetHitWidgetPath(Hit.Location, false, 1.0);
	if (HitTestGrid.IsValid())
	{
		for (FWidgetAndPointer& ArrangedWidget : ArrangedWidgets)
		{
			// Create a new touch pointer event and call mouse move on our widget
			ArrangedWidget.Widget->OnMouseMove(ArrangedWidget.Geometry, FPointerEvent(0, 0, ArrangedWidget.PointerPosition->CurrentCursorPosition, ArrangedWidget.PointerPosition->LastCursorPosition, PressLeftMouseButton));
		}
	}
}

void UVRWidgetComponent::EmulateActivateKeyDown(FHitResult Hit)
{
	TArray<FWidgetAndPointer> ArrangedWidgets = GetHitWidgetPath(Hit.Location, false, 1.0);
	if (HitTestGrid.IsValid())
	{
		for (FWidgetAndPointer& ArrangedWidget : ArrangedWidgets)
		{
			// Create a new key event with for the enter key to simulate a activate key down
			ArrangedWidget.Widget->OnKeyDown(ArrangedWidget.Geometry, FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
		}
	}
}

void UVRWidgetComponent::EmulateActivateKeyUp(FHitResult Hit)
{
	TArray<FWidgetAndPointer> ArrangedWidgets = GetHitWidgetPath(Hit.Location, false, 1.0);
	if (HitTestGrid.IsValid())
	{
		for (FWidgetAndPointer& ArrangedWidget : ArrangedWidgets)
		{
			// Create a new key event with for the enter key to simulate a activate key up
			ArrangedWidget.Widget->OnKeyUp(ArrangedWidget.Geometry, FKeyEvent(EKeys::Enter, FModifierKeysState(), 0, false, 0, 0));
		}
	}
}