// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Components/WidgetComponent.h"
#include "VRWidgetComponent.generated.h"

/**
 * 
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class UVRWidgetComponent : public UWidgetComponent
{
	GENERATED_BODY()
	
public:

	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	/** Converts a world hit location to a local hit location */
	UFUNCTION(BlueprintCallable, Category = "VR Widget")
	void GetLocalHit(FVector WorldHitLocation, FVector2D& OutLocalHitLocation);

	/** Tells the Widget to ignore mouse events and use this custom hit event */
	UFUNCTION(BlueprintCallable, Category = "VR Widget")
	void SetCustomHit(FHitResult Hit, bool SimulateHover);

	/** Focus this widget */
	UFUNCTION(BlueprintCallable, Category = "VR Widget")
	void Focus(APlayerController* PC);

	/** Emulates a touch press event for the given hit */
	UFUNCTION(BlueprintCallable, Category = "VR Widget")
	void EmulateTouchDown(FHitResult Hit, bool PressLeftMouseButton);

	/** Emulates a touch release event for the given hit */
	UFUNCTION(BlueprintCallable, Category = "VR Widget")
	void EmulateTouchUp(FHitResult Hit, bool PressLeftMouseButton);

	/** Emulates a touch move event for the given hit */
	UFUNCTION(BlueprintCallable, Category = "VR Widget")
	void EmulateTouchMove(FHitResult Hit, bool PressLeftMouseButton);

	/** Emulates an 'enter' key down event */
	UFUNCTION(BlueprintCallable, Category = "VR Widget")
	void EmulateActivateKeyDown(FHitResult Hit);

	/** Emulates an 'enter' key up event */
	UFUNCTION(BlueprintCallable, Category = "VR Widget")
	void EmulateActivateKeyUp(FHitResult Hit);
	
protected:
	/** The hit tester to use for this component */
	TSharedPtr<class FWidgetVRHitTester> WidgetHitTester;

	/** Used for simulating hover */
	TArray<FWidgetAndPointer> HoveredWidgets;

};
