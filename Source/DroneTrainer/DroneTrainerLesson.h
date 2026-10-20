#pragma once
#include "CoreMinimal.h"
#include "DroneTrainerGate.h"

// Pure predicates shared by runtime and Automation tests; all distances are centimetres.
namespace DroneLesson
{
inline float ProjectedHalf(const FQuat& Q, const FVector& E, const FVector& Axis)
{
    return FMath::Abs(Q.GetAxisX().Dot(Axis))*E.X + FMath::Abs(Q.GetAxisY().Dot(Axis))*E.Y + FMath::Abs(Q.GetAxisZ().Dot(Axis))*E.Z;
}
inline bool CrossesGate(const ADroneTrainerGate& G, FVector P0, FVector P1, FQuat Q0, FQuat Q1, FVector Extent, bool Armed)
{
    if (!Armed || P0.ContainsNaN() || P1.ContainsNaN() || Q0.ContainsNaN() || Q1.ContainsNaN()) return false;
    const FVector C=G.GetOpeningCenter(), N=G.GetActorForwardVector(), R=G.GetActorRightVector(), U=G.GetActorUpVector();
    const double D0=(P0-C).Dot(N), D1=(P1-C).Dot(N);
    if (D0>=0 || D1<0 || D1-D0<=UE_SMALL_NUMBER) return false;
    const double Alpha=D0/(D0-D1);
    if (!FMath::IsFinite(Alpha) || Alpha<0 || Alpha>1) return false;
    const FVector P=FMath::Lerp(P0,P1,Alpha)-C;
    // Bound every intermediate orientation, including a fast turn within this segment.
    const float RotationMargin=2*Extent.Size()*FMath::Sin(Q0.AngularDistance(Q1)*0.5f);
    const float HalfR=FMath::Min(float(Extent.Size()),FMath::Max(ProjectedHalf(Q0,Extent,R),ProjectedHalf(Q1,Extent,R))+RotationMargin);
    const float HalfU=FMath::Min(float(Extent.Size()),FMath::Max(ProjectedHalf(Q0,Extent,U),ProjectedHalf(Q1,Extent,U))+RotationMargin);
    return FMath::Abs(P.Dot(R))+HalfR+5<=G.OpeningHalfSize.X+0.0001 && FMath::Abs(P.Dot(U))+HalfU+5<=G.OpeningHalfSize.Y+0.0001;
}
// Empty means a soft contact; the same checks supply the player-facing failure reason.
inline FString SurfaceContactFailure(FVector P, FQuat Q, FVector Extent, FVector Velocity, FVector Normal, FVector Contact)
{
    if (P.ContainsNaN() || Q.ContainsNaN() || Velocity.ContainsNaN() || Normal.ContainsNaN() || Contact.ContainsNaN())
        return TEXT("Не удалось определить параметры касания");
    // Round excess speed upward so a rejected value never displays as within the limit.
    if (Velocity.Size2D()>50)
        return FString::Printf(TEXT("Горизонтальная скорость: %.2f > 0,5 м/с"),FMath::CeilToDouble(Velocity.Size2D())/100).Replace(TEXT("."),TEXT(","));
    if (FMath::Abs(Velocity.Z)>40)
        return FString::Printf(TEXT("Вертикальная скорость: %.2f > 0,4 м/с"),FMath::CeilToDouble(FMath::Abs(Velocity.Z))/100).Replace(TEXT("."),TEXT(","));
    const FRotator Angles=Q.Rotator();
    if (FMath::Abs(Angles.Pitch)>10 || FMath::Abs(Angles.Roll)>10) return TEXT("Наклон дрона при касании больше 10°");
    if (Normal.Z<FMath::Cos(FMath::DegreesToRadians(10.f))) return TEXT("Удар о боковую или наклонную поверхность");
    if (Contact.Z>P.Z-ProjectedHalf(Q,Extent,FVector::UpVector)+3) return TEXT("Касание корпусом: выровняйте дрон");
    return FString();
}
inline bool SoftSurfaceContact(FVector P, FQuat Q, FVector Extent, FVector Velocity, FVector Normal, FVector Contact)
{
    return SurfaceContactFailure(P,Q,Extent,Velocity,Normal,Contact).IsEmpty();
}
inline bool SoftLanding(FVector P, FQuat Q, FVector Extent, FVector Velocity, FVector Normal, FVector Contact, const FBox& Pad)
{
    if (!Pad.IsValid || !SoftSurfaceContact(P,Q,Extent,Velocity,Normal,Contact)) return false;
    const FVector Half(ProjectedHalf(Q,Extent,FVector::ForwardVector),ProjectedHalf(Q,Extent,FVector::RightVector),ProjectedHalf(Q,Extent,FVector::UpVector));
    return P.X-Half.X>=Pad.Min.X+50 && P.X+Half.X<=Pad.Max.X-50 && P.Y-Half.Y>=Pad.Min.Y+50 && P.Y+Half.Y<=Pad.Max.Y-50;
}
inline float Hold(float Previous, float Dt, bool Valid) { return Valid ? Previous+Dt : 0.f; }
inline int32 Outcome(bool Invalid, bool Collision, bool Boundary, float Elapsed)
{
    return Invalid ? 1 : Collision ? 2 : Boundary ? 3 : Elapsed>=300 ? 4 : 0;
}
}
