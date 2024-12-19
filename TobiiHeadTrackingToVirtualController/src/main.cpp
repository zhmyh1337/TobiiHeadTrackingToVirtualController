#include <iomanip>
#include <iostream>
#include <algorithm>
#include <Windows.h>
#include <Xinput.h>
#include "ViGEm/Client.h"
#include "tobii_gameintegration.h"

using namespace TobiiGameIntegration;

HWND GetConsoleHwnd(void); // See SampleHelpFunctions.cpp

#pragma region UserDefinedConfig
static constexpr float k_maxYawIRL = 50.0f;
static constexpr float k_maxPitchIRL = 30.0f;
static constexpr float k_deadYawIRL = 5.0f;
static constexpr float k_deadPitchIRL = 7.5f;
static constexpr bool k_addControllerStickInput = false;

static std::pair<float, float> IRLYawPitchToControllerStick(float irlYaw, float irlPitch)
{
    if (std::abs(irlYaw) < k_deadYawIRL)
    {
        irlYaw = 0.0f;
    }
    else
    {
        irlYaw -= std::copysign(k_deadYawIRL, irlYaw);
    }

    if (irlPitch < k_deadPitchIRL)
    {
        irlPitch = 0.0f;
    }
    else
    {
        irlPitch -= std::copysign(k_deadPitchIRL, irlPitch);
    }

    float controllerYaw = irlYaw / (k_maxYawIRL - k_deadYawIRL);
    controllerYaw = std::clamp(controllerYaw, -1.0f, 1.0f);

    float controllerPitch = irlPitch / (k_maxPitchIRL - k_deadPitchIRL);
    controllerPitch = std::clamp(controllerPitch, -1.0f, 1.0f);

    return { controllerYaw, controllerPitch };
}
#pragma endregion

static VOID CALLBACK OnVirtualControllerNotification(
    PVIGEM_CLIENT client,
    PVIGEM_TARGET target,
    UCHAR largeMotor,
    UCHAR smallMotor,
    UCHAR ledNumber,
    LPVOID userData
)
{
    XINPUT_VIBRATION vibration;
    vibration.wLeftMotorSpeed = static_cast<WORD>(static_cast<float>(largeMotor) / 255.0f * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(static_cast<float>(smallMotor) / 255.0f * 65535.0f);
    XInputSetState(1, &vibration);
}

int main()
{
    {
        XINPUT_STATE inputState;
        if (XInputGetState(0, &inputState) == ERROR_SUCCESS)
        {
            std::cerr << "Controller 0 is already connected. Please disconnect it before running this program." << std::endl;
            std::cerr << "Press Enter to ignore this warning and continue..." << std::endl;
            std::cin.get();
        }
    }

    // Initialize ViGEm
    const auto client = vigem_alloc();
    if (client == nullptr)
    {
        std::cerr << "Failed to allocate ViGEm client." << std::endl;
        return -1;
    }

    {
        const auto retval = vigem_connect(client);
        if (!VIGEM_SUCCESS(retval))
        {
            std::cerr << "ViGEm Bus connection failed with error code: 0x" << std::hex << retval << std::endl;
            return -1;
        }
    }

    const auto pad = vigem_target_x360_alloc();
    const auto pir = vigem_target_add(client, pad);
    if (!VIGEM_SUCCESS(pir))
    {
        std::cerr << "Target plugin failed with error code: 0x" << std::hex << pir << std::endl;
        return -1;
    }

    {
        const auto retval = vigem_target_x360_register_notification(client, pad, &OnVirtualControllerNotification, nullptr);
        if (!VIGEM_SUCCESS(retval))
        {
            std::cerr << "Registering for OnVirtualControllerNotification failed with error code: 0x" << std::hex << retval << std::endl;
            return -1;
        }
    }

    // Initialize Tobii
    ITobiiGameIntegrationApi* tobiiApi = GetApi("Extended View Sample");
    IExtendedView* extendedView = tobiiApi->GetFeatures()->GetExtendedView();

    // Turn on head tracking position
    ExtendedViewSettings extendedViewSettings;
    extendedView->GetSettings(extendedViewSettings);
    extendedViewSettings.HeadTracking.PositionEnabled = true;
    extendedView->UpdateSettings(extendedViewSettings);

    tobiiApi->GetTrackerController()->TrackWindow(GetConsoleHwnd());

    std::cout << "F8 to exit" << std::endl << std::endl;

    while (!GetAsyncKeyState(VK_F8))
    {
        Sleep(1);

        XUSB_REPORT virtualControllerState;
        XUSB_REPORT_INIT(&virtualControllerState);

        XINPUT_STATE inputState;
        if (XInputGetState(1, &inputState) == ERROR_SUCCESS)
        {
            virtualControllerState = *reinterpret_cast<XUSB_REPORT*>(&inputState.Gamepad);
        }

        tobiiApi->Update();

        const Transformation trans = extendedView->GetTransformation();

        std::cout << std::fixed << std::setprecision(3);
        std::cout << "Extended View Rot(deg) [Y: " << trans.Rotation.YawDegrees << ",P: " << trans.Rotation.PitchDegrees << ",R: " << trans.Rotation.RollDegrees << "] " <<
            "Pos(mm) [X: " << trans.Position.X << ",Y: " << trans.Position.Y << ",Z: " << trans.Position.Z << "]          \r";

        auto [yaw, pitch] = IRLYawPitchToControllerStick(trans.Rotation.YawDegrees, trans.Rotation.PitchDegrees);
        if (k_addControllerStickInput)
        {
            yaw += static_cast<float>(inputState.Gamepad.sThumbRX) / 32767.0f;
            pitch += static_cast<float>(inputState.Gamepad.sThumbRY) / 32767.0f;
        }
        yaw = std::clamp(yaw, -1.0f, 1.0f);
        pitch = std::clamp(pitch, -1.0f, 1.0f);
        float magnitude = std::sqrt(yaw * yaw + pitch * pitch);
        if (magnitude > 1.0f)
        {
            yaw /= magnitude;
            pitch /= magnitude;
        }
        virtualControllerState.sThumbRX = static_cast<SHORT>(yaw * 32767.0f);
        virtualControllerState.sThumbRY = static_cast<SHORT>(pitch * 32767.0f);

        const auto retval = vigem_target_x360_update(client, pad, virtualControllerState);
        if (!VIGEM_SUCCESS(retval))
        {
            std::cerr << "Target update failed with error code: 0x" << std::hex << retval << std::endl;
            break;
        }
    }

    // Cleanup ViGEm
    vigem_target_remove(client, pad);
    vigem_target_free(pad);
    vigem_disconnect(client);
    vigem_free(client);

	// Cleanup Tobii
    tobiiApi->Shutdown();

    return 0;
}
