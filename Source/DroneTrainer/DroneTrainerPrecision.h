#pragma once
#include "CoreMinimal.h"
#include "DroneTrainerLesson.h"

// Position and velocity are centimetres; AGL is metres, heading is degrees.
namespace DronePrecision
{
enum class EStep : uint8 { Takeoff, P1, P2, Turn, P3, Landing };
inline int32 TargetIndex(EStep Step)
{
    return Step==EStep::Takeoff ? 0 : Step==EStep::P1 ? 1 :
        Step==EStep::P2 || Step==EStep::Turn ? 2 : Step==EStep::P3 ? 3 : 4;
}
inline FString HoldFailure(FVector Position, FVector Target, float AGL, bool GroundValid,
    float HorizontalSpeed, float Yaw, float RequiredYaw, bool HeadingRequired)
{
    if (!GroundValid || !FMath::IsFinite(AGL)) return TEXT("Нет достоверной высоты над землёй");
    if (FVector::Dist2D(Position,Target)>200) return TEXT("Вернитесь к центру");
    if (AGL<2.5f) return TEXT("Поднимитесь немного выше");
    if (AGL>3.5f) return TEXT("Снизьтесь до учебной высоты");
    if (!FMath::IsFinite(HorizontalSpeed) || HorizontalSpeed>0.5f) return TEXT("Снизьте скорость");
    if (HeadingRequired && FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw,RequiredYaw))>15) return TEXT("Выровняйте курс");
    return TEXT("");
}
inline FString Correction(FVector Delta, float Yaw)
{
    const FRotationMatrix Basis(FRotator(0,Yaw,0));
    const double Forward=Delta.Dot(Basis.GetUnitAxis(EAxis::X))/100;
    const double Right=Delta.Dot(Basis.GetUnitAxis(EAxis::Y))/100;
    if (Delta.Size2D()<=200) return TEXT("В допустимой области — удерживайте центр");
    return FString::Printf(TEXT("%s %.1f м  •  %s %.1f м"),Forward>=0 ? TEXT("Вперёд") : TEXT("Назад"),FMath::Abs(Forward),
        Right>=0 ? TEXT("Правее") : TEXT("Левее"),FMath::Abs(Right));
}
inline bool Advance(EStep& Step,float& Held,float Dt,bool Running,bool GroundValid,float AGL,bool ValidHold)
{
    if (!Running) return false;
    if (Step==EStep::Takeoff)
    {
        if (!GroundValid || !FMath::IsFinite(AGL) || AGL<3) return false;
    }
    else
    {
        if (Step==EStep::Landing) return false;
        Held=DroneLesson::Hold(Held,Dt,ValidHold);
        if (Held<3) return false;
    }
    Step=EStep(uint8(Step)+1); Held=0; return true;
}
}
