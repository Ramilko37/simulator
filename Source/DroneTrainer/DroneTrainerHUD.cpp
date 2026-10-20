#include "DroneTrainerHUD.h"
#include "DroneTrainerSettings.h"
#include "DroneTrainerDronePawn.h"
#include "DroneTrainerGate.h"
#include "DroneTrainerLevelLayout.h"
#include "Engine/Canvas.h"
#include "Engine/StaticMeshActor.h"
#include "GameFramework/PlayerStart.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "CanvasTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/SlateRenderer.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/KismetSystemLibrary.h"

static FSlateFontInfo HUDPixelFont(UFont* Font,float Height)
{
    FSlateFontInfo Info=Font->GetLegacySlateFontInfo();
    Info.Size=FMath::Max(1.f,Info.Size*Height/Font->GetMaxCharHeight());
    return Info;
}

void ADroneTrainerHUD::DrawHUD()
{
    Super::DrawHUD();
    const ADroneTrainerDronePawn* Drone = Cast<ADroneTrainerDronePawn>(GetOwningPawn());
    APlayerController* PC = GetOwningPlayerController();
    if (!Canvas || Canvas->ClipX<=0 || Canvas->ClipY<=0 || !GEngine || !PC) return;
    if (!Drone || !PC->PlayerCameraManager)
    {
        DrawText(TEXT("Ошибка подготовки дрона. Q — выход"),FLinearColor::White,24,24,GEngine->GetSmallFont());
        if (PC->WasInputKeyJustPressed(EKeys::Q)) UKismetSystemLibrary::QuitGame(this,PC,EQuitPreference::Quit,false);
        return;
    }

    // Canvas uses output pixels, independent of the scene's ScreenPercentage.
    const float S = FMath::Min(Canvas->ClipX / 1920.0f, Canvas->ClipY / 1080.0f);
    const float W = Canvas->ClipX / S, H = Canvas->ClipY / S;
    const FLinearColor White(0.95f, 0.97f, 1), Orange(1, 0.65f, 0.22f), Back(0.015f, 0.02f, 0.03f, 0.88f);
    UFont* Font = GEngine->GetSmallFont();
    const auto FontMeasure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
    auto Text = [&](const FString& Value, float X, float Y, float Size = 20, FLinearColor Color = FLinearColor::White)
    {
        // Rasterize glyphs at their final pixel size instead of enlarging a small cached bitmap.
        const FSlateFontInfo Info=HUDPixelFont(Font,Size*S);
        const FVector2D Bounds=FontMeasure->Measure(Value,Info,Canvas->Canvas->GetDPIScale());
        const FVector2D P(FMath::Clamp(X*S,0.f,FMath::Max(0.f,Canvas->ClipX-float(Bounds.X)-8*S)),
            FMath::Clamp(Y*S,0.f,FMath::Max(0.f,Canvas->ClipY-float(Bounds.Y)-8*S)));
        FCanvasTextItem Item(P,FText::FromString(Value),Info,Color);
        Canvas->DrawItem(Item);
    };
    auto Panel = [&](float X, float Y, float Width, float Height) { DrawRect(Back, X*S, Y*S, Width*S, Height*S); };
    auto Line = [&](FVector2D A, FVector2D B, FLinearColor Color = FLinearColor::White, float Width = 1.5f)
    { DrawLine(A.X*S, A.Y*S, B.X*S, B.Y*S, Color, Width*S); };
    const EDroneAttemptState State = Drone->GetAttemptState();
    const ADroneTrainerGate* Gate = Drone->GetLessonPhase()==EDroneLessonPhase::Route ? Drone->GetCurrentGate() : nullptr;
    FString Instruction = Drone->GetLessonInstruction();
    const bool Precision=Drone->IsPrecisionLesson();
    FString Status = Precision ? TEXT("Урок 2 • Точное позиционирование") : TEXT("Учебный полёт");
    if (State == EDroneAttemptState::MainMenu) { Status = TEXT("DRONE TRAINER"); Instruction = TEXT("Enter — начать  |  O — настройки  |  Q — выход"); }
    if (State == EDroneAttemptState::Countdown) Status = FString::Printf(TEXT("Старт через %d"), FMath::Max(1, FMath::CeilToInt(Drone->GetCountdownRemaining())));
    if (State == EDroneAttemptState::Paused) { Status = TEXT("ПАУЗА"); Instruction=TEXT("Esc — продолжить  |  O — настройки  |  M — меню  |  Q — выход"); }
    if (State == EDroneAttemptState::Failed)
    {
        switch (Drone->GetFailureReason())
        {
        case EDroneFailureReason::Collision: Status=TEXT("НЕУДАЧНОЕ КАСАНИЕ"); break;
        case EDroneFailureReason::TimeLimit: Status=TEXT("ВРЕМЯ ИСТЕКЛО"); break;
        case EDroneFailureReason::OutOfBounds: Status=TEXT("ВЫХОД ЗА ГРАНИЦЫ"); break;
        default: Status=TEXT("ПОПЫТКА НЕ УДАЛАСЬ"); break;
        }
        Instruction = Drone->GetFailureDetail();
        if (Instruction.IsEmpty()) Instruction = Drone->GetFailureReason()==EDroneFailureReason::TimeLimit
            ? TEXT("Урок не завершён за 5 минут") : Drone->GetFailureReason()==EDroneFailureReason::OutOfBounds
            ? TEXT("Дрон покинул учебную зону") : TEXT("Нажмите R для повтора");
    }
    if (State == EDroneAttemptState::Error) { Status=TEXT("ОШИБКА УРОВНЯ"); Instruction=TEXT("Не готова карта, дрон или маршрут. Q — выход"); }
    if (State == EDroneAttemptState::Completed) { Status = TEXT("УПРАЖНЕНИЕ ПРОЙДЕНО"); Instruction = TEXT("R — повтор  |  M — главное меню"); }
    if (State == EDroneAttemptState::Aborted) { Status = TEXT("ПОПЫТКА ЗАВЕРШЕНА"); Instruction = TEXT("R — повтор  |  M — главное меню"); }
    const bool LandingHint = !Precision && Drone->GetLessonPhase()==EDroneLessonPhase::Landing && State==EDroneAttemptState::Running && Drone->GetLandingSeconds()==0;
    Panel(24,24,Precision ? 580 : 460,LandingHint ? 124 : 88); Text(Status,40,36,26);
    if (LandingHint)
    {
        Text(TEXT("Перед посадкой: скорость по горизонтали"),40,76,18);
        Text(TEXT("не выше 0,5 м/с. Shift — снижение"),40,104,18);
    }
    else Text(Instruction,40,76,18);
    if (!Drone->GetPerformanceStatus().IsEmpty())
    {
        Panel(W/2-310,92,620,44);
        Text(Drone->GetPerformanceStatus(),W/2-294,102,18,Orange);
    }
    const FRotator Body = Drone->GetActorRotation();
    const int32 Heading = FMath::RoundToInt(FRotator::ClampAxis(Body.Yaw)) % 360;
    Panel(W/2-105,24,210,58); Text(FString::Printf(TEXT("КУРС  %03d°"),Heading),W/2-87,40,26);
    Panel(W-284,24,260,88);
    if (Precision) Text(FString::Printf(TEXT("Позиции %d / 3"),Drone->GetPositionsCompleted()),W-268,36,26);
    else Text(FString::Printf(TEXT("%d/%d • %s"),Drone->GetGatesPassed(),Drone->GetGateCount(),Drone->GetLessonPhase()==EDroneLessonPhase::Landing ? TEXT("Посадка") : TEXT("Ворота")),W-268,36,26);
    const int32 Seconds = FMath::FloorToInt(Drone->GetElapsedSeconds());
    Text(Precision ? FString::Printf(TEXT("Время %02d:%02d"),Seconds/60,Seconds%60) : FString::Printf(TEXT("%02d:%02d / 05:00"),Seconds/60,Seconds%60),W-268,76);

    Panel(24,H-124,285,100); Text(FString::Printf(TEXT("%.1f м/с  горизонт."),Drone->GetHorizontalSpeed()),40,H-108,26);
    Text(FString::Printf(TEXT("%+.1f м/с  вертик."),Drone->GetVerticalSpeed()),40,H-64,24);
    Panel(W-284,H-124,260,100);
    Text(Drone->HasGroundSurface() ? FString::Printf(TEXT("%.1f м AGL"),Drone->GetAltitudeMeters()) : TEXT("— м AGL"),W-268,H-108,28);
    Text(TEXT("над землёй"),W-268,H-64);
    Panel(W/2-250,H-70,500,46);
    Text(!Drone->HasTakenOff() ? TEXT("Space / Shift — высота  |  Esc — пауза") : TEXT("C — камера  |  Esc — пауза"),W/2-234,H-58);

    const FVector Position = Drone->GetActorLocation();
    const ADroneTrainerLevelLayout* Layout = Drone->GetLayout();
    const bool Boundary = Layout && (FMath::Abs(Position.X) > Layout->WarningXY || FMath::Abs(Position.Y) > Layout->WarningXY ||
        (Drone->HasGroundSurface() && Drone->GetAltitudeMeters()*100 > Layout->WarningAGL));
    FString Warning = Boundary ? TEXT("! Граница зоны — поверните к полю") :
        !Drone->HasGroundSurface() ? TEXT("! Нет поверхности под дроном") : Drone->IsLowAltitude() ? TEXT("! Низкая высота — наберите высоту") : TEXT("");
    if (Drone->GetBoundarySeconds()>0) Warning=FString::Printf(TEXT("! Вернитесь в зону: %.1f с"),FMath::Max(0.f,5-Drone->GetBoundarySeconds()));
    if (!Warning.IsEmpty()) { const float Y=LandingHint ? 160 : 124; Panel(24,Y,490,48); Text(Warning,40,Y+12,20,Orange); }

    const FVector2D Center(W/2,H/2);
    Line(Center-FVector2D(10,0),Center-FVector2D(3,0)); Line(Center+FVector2D(3,0),Center+FVector2D(10,0));
    Line(Center-FVector2D(0,10),Center-FVector2D(0,3)); Line(Center+FVector2D(0,3),Center+FVector2D(0,10));
    const float Roll = FMath::DegreesToRadians(Body.Roll);
    const FVector2D Right(FMath::Cos(Roll),-FMath::Sin(Roll));
    const FVector2D Horizon = Center + FVector2D(0,FMath::Clamp(Body.Pitch*3.0f,-120.0f,120.0f));
    Line(Horizon-Right*100,Horizon-Right*24,White); Line(Horizon+Right*24,Horizon+Right*100,White);

    auto Marker = [&](FVector Target, const FString& Label, FLinearColor Color)
    {
        const FVector Camera = PC->PlayerCameraManager->GetCameraLocation();
        const FRotationMatrix Basis(PC->PlayerCameraManager->GetCameraRotation());
        const FVector Delta = Target-Camera;
        FVector2D Pixel = FVector2D::ZeroVector;
        const bool Front = FVector::DotProduct(Delta,Basis.GetUnitAxis(EAxis::X)) > 0;
        const bool Projected = PC->ProjectWorldLocationToScreen(Target,Pixel);
        FVector2D P = Pixel/S;
        if (!(Front && Projected && P.X > 80 && P.X < W-240 && P.Y > 200 && P.Y < H-170))
        {
            FVector2D Direction(FVector::DotProduct(Delta,Basis.GetUnitAxis(EAxis::Y)), -FVector::DotProduct(Delta,Basis.GetUnitAxis(EAxis::Z)));
            if (Direction.IsNearlyZero()) Direction = FVector2D(0,1);
            Direction.Normalize();
            const float T = FMath::Min((W/2-100)/FMath::Max(FMath::Abs(Direction.X),0.001f), (H/2-200)/FMath::Max(FMath::Abs(Direction.Y),0.001f));
            P = Center + Direction*T;
            const FVector2D Side(-Direction.Y,Direction.X);
            Line(P,P-Direction*22+Side*12,Color,3); Line(P,P-Direction*22-Side*12,Color,3);
        }
        else
        {
            Line(P-FVector2D(12,0),P-FVector2D(0,12),Color,2); Line(P-FVector2D(0,12),P+FVector2D(12,0),Color,2);
            Line(P+FVector2D(12,0),P+FVector2D(0,12),Color,2); Line(P+FVector2D(0,12),P-FVector2D(12,0),Color,2);
        }
        if ((P-Center).Size() < W*0.1f) P.X=Center.X+W*0.1f+24;
        P.X=FMath::Clamp(P.X,24.0f,W-245); P.Y=FMath::Clamp(P.Y+22,184.0f,H-158);
        Panel(P.X-8,P.Y-4,230,36); Text(Label,P.X,P.Y,20,Color);
    };
    if (!Precision && !Gate && Drone->GetLessonPhase()==EDroneLessonPhase::Landing && (State==EDroneAttemptState::Running || State==EDroneAttemptState::Paused))
        Marker(Drone->GetLessonTarget(),TEXT("Посадочная площадка"),Orange);
    if (!Precision) for (TActorIterator<ADroneTrainerGate> It(GetWorld());It;++It)
    {
        FVector2D P;
        const FVector Top=It->GetOpeningCenter()+It->GetActorUpVector()*230;
        if (FVector::Dist(Position,Top)<10000 && (Top-PC->PlayerCameraManager->GetCameraLocation()).Dot(PC->PlayerCameraManager->GetActorForwardVector())>0 && PC->ProjectWorldLocationToScreen(Top,P) && P.X>=0 && P.X<Canvas->ClipX && P.Y>=0 && P.Y<Canvas->ClipY)
            Text(FString::Printf(TEXT("%s G%d"),It->GetLessonState()==2 ? TEXT("✓") : TEXT("→"),It->GateIndex),P.X/S,P.Y/S,24,It->GetLessonState()==1 ? Orange : White);

    }
    if (Precision && (State==EDroneAttemptState::Running || State==EDroneAttemptState::Paused))
    {
        const float Distance=FVector::Dist2D(Position,Drone->GetLessonTarget())/100;
        Marker(Drone->GetLessonTarget(),FString::Printf(TEXT("%s • %.1f м"),*Drone->GetPrecisionTargetName(),Distance),Orange);
        Panel(24,200,610,240);
        Text(FString::Printf(TEXT("%s • до центра %.1f м"),*Drone->GetPrecisionTargetName(),Distance),40,214,24);
        Text(Drone->GetPositionCorrection(),40,250,20);
        const auto Step=Drone->GetPrecisionStep();
        const bool Hold=Step!=DronePrecision::EStep::Takeoff && Step!=DronePrecision::EStep::Landing;
        Text(Hold ? FString::Printf(TEXT("Удержание %.1f / 3 с • %s"),Drone->GetPositionHoldSeconds(),
            Drone->GetPrecisionFeedback().IsEmpty() ? TEXT("Продолжайте") : *Drone->GetPrecisionFeedback()) :
            Step==DronePrecision::EStep::Landing ? TEXT("Мягкое касание B2, затем 2 с покоя") : TEXT("Space — взлёт до 3 м AGL"),40,286,20,Orange);
        const bool HeadingRequired=Step==DronePrecision::EStep::P2 || Step==DronePrecision::EStep::Turn || Step==DronePrecision::EStep::P3;
        Text(HeadingRequired ? FString::Printf(TEXT("Курс %.0f° ±15° • отклонение %+.0f°"),FRotator::ClampAxis(Drone->GetRequiredHeading()),
            FMath::FindDeltaAngleDegrees(Drone->GetRequiredHeading(),Body.Yaw)) : TEXT("Направления коррекции — относительно носа дрона"),40,322,20);
        Text(TEXT("Допуски: центр ≤2 м • AGL 2,5–3,5 м"),40,358,20);
        Text(TEXT("Скорость ≤0,5 м/с • непрерывно 3 с"),40,394,20);
    }
    if (Gate) Marker(Gate->GetOpeningCenter(), FString::Printf(TEXT("G%d  •  %.0f м"),Gate->GateIndex,FVector::Distance(Position,Gate->GetOpeningCenter())/100),Orange);
    if (Boundary)
    {
        const FVector Return(FMath::Clamp(Position.X,-Layout->WarningXY+100,Layout->WarningXY-100),
            FMath::Clamp(Position.Y,-Layout->WarningXY+100,Layout->WarningXY-100),Position.Z- (Drone->GetAltitudeMeters()*100 > Layout->WarningAGL ? 500 : 0));
        Marker(Return,TEXT("К полю"),White);
    }
    if (State == EDroneAttemptState::Paused || State == EDroneAttemptState::Failed || State == EDroneAttemptState::MainMenu ||
        State == EDroneAttemptState::Completed || State == EDroneAttemptState::Aborted || State == EDroneAttemptState::Error)
    {
        const auto* Settings=Cast<UDroneTrainerSettings>(GEngine->GetGameUserSettings());
        const bool SettingsOpen=Drone->IsSettingsOpen() && Settings;
        const float X=W/2-380, Y=H/2-110;
        Panel(X,Y,760,SettingsOpen ? 360 : State==EDroneAttemptState::MainMenu ? 390 : Precision ? 320 : 270);
        Text(SettingsOpen ? TEXT("НАСТРОЙКИ") : Status,X+24,Y+20,28);
        if (SettingsOpen)
        {
            Text(FString::Printf(TEXT("1 / 2 — качество: %s"),Settings->GetProfile()==0 ? TEXT("Низкое") : TEXT("Среднее")),X+24,Y+70,22);
            Text(FString::Printf(TEXT("3 / 4 — чувствительность: %.1f"),Settings->Sensitivity),X+24,Y+112,22);
            Text(FString::Printf(TEXT("5 / 6 — общая громкость: %.0f %%"),Settings->MasterVolume*100),X+24,Y+154,22);
            Text(FString::Printf(TEXT("F — режим: %s"),Settings->GetFullscreenMode()==EWindowMode::Windowed ? TEXT("Окно") : TEXT("Полный экран")),X+24,Y+196,22);
            const FIntPoint Resolution=Settings->GetScreenResolution();
            Text(Settings->GetFullscreenMode()==EWindowMode::WindowedFullscreen ? TEXT("Разрешение: по экрану") :
                FString::Printf(TEXT("V — разрешение: %d × %d"),Resolution.X,Resolution.Y),X+24,Y+238,22);
            Text(TEXT("Сохраняется автоматически. O / Esc — назад"),X+24,Y+304,20,Orange);
        }
        else
        {
            Text(Instruction,X+24,Y+70,18);
            if (State==EDroneAttemptState::MainMenu)
            {
                Text(Precision ? TEXT("←  Урок 1 — Базовый полёт") : TEXT("←  ✓ Урок 1 — Базовый полёт"),X+24,Y+112,24);
                Text(Precision ? TEXT("→  ✓ Урок 2 — Точное позиционирование") : TEXT("→  Урок 2 — Точное позиционирование"),X+24,Y+156,24);
                Text(TEXT("Стрелки ← / → — выбор; Enter — начать"),X+24,Y+204,20,Orange);
                Text(TEXT("W/S — тангаж | A/D — крен | Q/E — разворот"),X+24,Y+252,20);
                Text(TEXT("Space/Shift — высота | C — камера | Esc — пауза"),X+24,Y+296,20);
                if (!Drone->GetFailureDetail().IsEmpty()) Text(Drone->GetFailureDetail(),X+24,Y+344,18,Orange);
                return;
            }
            if (Precision)
            {
                Text(FString::Printf(TEXT("Позиции: %d / 3 • Поворот: %s • Время: %02d:%02d"),Drone->GetPositionsCompleted(),
                    Drone->GetPrecisionStep()==DronePrecision::EStep::P3 || Drone->GetPrecisionStep()==DronePrecision::EStep::Landing ? TEXT("выполнен") : TEXT("не выполнен"),Seconds/60,Seconds%60),X+24,Y+112,22);
                Text(State==EDroneAttemptState::Completed ? TEXT("Посадка B2: мягкая, контакт 2 с — зачтено") : TEXT("Посадка B2: не зачтена"),X+24,Y+160,20);
                Text(TEXT("R — повтор | M — меню | Q — выход"),X+24,Y+210,20);
                Text(TEXT("Space/Shift — высота | C — камера | Esc — пауза"),X+24,Y+260,20);
                return;
            }
            Text(FString::Printf(TEXT("Ворота: %d / 5    Время: %02d:%02d"),Drone->GetGatesPassed(),Seconds/60,Seconds%60),X+24,Y+112,22);
            Text(State==EDroneAttemptState::MainMenu ? TEXT("W/S — тангаж  |  A/D — крен  |  Q/E — разворот") : TEXT("R — повтор  |  M — меню  |  Q — выход"),X+24,Y+160,20);
            Text(TEXT("Space/Shift — высота  |  C — камера  |  Esc — пауза"),X+24,Y+210,20);
        }
    }
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "EngineFontServices.h"
#include "Fonts/FontCache.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDroneTrainerHUDTest,"DroneTrainer.HUD",EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FDroneTrainerHUDTest::RunTest(const FString&)
{
    UFont* Font=GEngine->GetSmallFont();
    const auto Cache=FEngineFontServices::Get().GetFontCache();
    if (!TestTrue(TEXT("HUD font and raster cache available"),Font && Cache.IsValid())) return false;
    const auto Small=Cache->GetCharacterList(HUDPixelFont(Font,20),1).GetCharacter(TEXT('Ж'),EFontFallback::FF_Max);
    const auto Large=Cache->GetCharacterList(HUDPixelFont(Font,40),1).GetCharacter(TEXT('Ж'),EFontFallback::FF_Max);
    TestTrue(TEXT("Cyrillic has real glyphs"),Small.Valid && Large.Valid && Small.GlyphIndex!=0 && Large.GlyphIndex!=0);
    TestTrue(TEXT("Retina gets a larger raster, not the same bitmap scaled up"),Small.VSize>0 && Large.VSize>=Small.VSize*1.75f);
    UE_LOG(LogTemp,Display,TEXT("HUD_FONT_CHECK cyrillic_raster_1x=%d cyrillic_raster_2x=%d bitmap_scale_2x=%.3f"),Small.VSize,Large.VSize,Large.BitmapRenderScale);
    return true;
}
#endif
