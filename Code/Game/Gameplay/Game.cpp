//----------------------------------------------------------------------------------------------------
// Game.cpp
//----------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------
#include "Game/Gameplay/Game.hpp"

#include "Engine/Core/Clock.hpp"
#include "Engine/Core/DevConsole.hpp"
#include "Engine/Core/EngineCommon.hpp"
#include "Engine/Core/ErrorWarningAssert.hpp"
#include "Engine/Input/InputSystem.hpp"
#include "Engine/Network/NetworkSubsystem.hpp"
#include "Engine/Renderer/DebugRenderSystem.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Engine/Platform/Window.hpp"
#include "Engine/Resource/ResourceLoader/ObjModelLoader.hpp"
#include "Game/Definition/BoardDefinition.hpp"
#include "Game/Definition/PieceDefinition.hpp"
#include "Game/Framework/App.hpp"
#include "Game/Framework/GameCommon.hpp"
#include "Game/Framework/PlayerController.hpp"
#include "Game/Gameplay/Match.hpp"

//----------------------------------------------------------------------------------------------------
Game::Game()
{
    g_theEventSystem->SubscribeEventCallbackFunction("OnGameStateChanged", OnGameStateChanged);
    g_theEventSystem->SubscribeEventCallbackFunction("ChessConnect", Event_ChessConnect);
    g_theEventSystem->SubscribeEventCallbackFunction("ChessListen", Event_ChessListen);
    g_theEventSystem->SubscribeEventCallbackFunction("ChessPlayerInfo", Event_ChessPlayerInfo);
    m_gameClock                 = new Clock(Clock::GetSystemClock());
    m_screenCamera              = new Camera();
    Vec2 const bottomLeft       = Vec2::ZERO;
    Vec2    clientDimensions = Window::s_mainWindow->GetClientDimensions();
    Vec2 const screenTopRight   = Vec2(clientDimensions.x, clientDimensions.y);
    m_screenCamera->SetOrthoGraphicView(bottomLeft, screenTopRight);
    m_screenCamera->SetNormalizedViewport(AABB2::ZERO_TO_ONE);
    CreateLocalPlayer(0);
    CreateLocalPlayer(1);
    UpdateCurrentControllerId(0);
}

//----------------------------------------------------------------------------------------------------
Game::~Game()
{
}

//----------------------------------------------------------------------------------------------------
void Game::Update()
{
    float const gameDeltaSeconds   = static_cast<float>(m_gameClock->GetDeltaSeconds());
    float const systemDeltaSeconds = static_cast<float>(Clock::GetSystemClock().GetDeltaSeconds());

    // #TODO: Select keyboard or controller
    UpdateEntities(gameDeltaSeconds, systemDeltaSeconds);

    UpdateFromInput();

    // 網路連線狀態檢查
    static bool s_hasSentPlayerInfo = false;
    static bool s_wasConnected = false;

    if (g_theNetworkSubsystem)
    {
        bool isConnected = g_theNetworkSubsystem->IsConnected();

        // Client 端：剛連上時自動發送玩家資訊
        if (g_theNetworkSubsystem->IsClient() && isConnected && !s_hasSentPlayerInfo)
        {
            // 從設定或介面取得玩家名稱，這裡先用固定值
            std::string myName = m_playerName;  // 或使用 g_theGame->m_playerName
            std::string command = Stringf("ChessPlayerInfo name=%s", myName.c_str());

            g_theNetworkSubsystem->SendRawData(command);
            s_hasSentPlayerInfo = true;

            g_theDevConsole->AddLine(DevConsole::INFO_MAJOR,
                Stringf("Sent player info: %s", myName.c_str()));
        }

        // 如果斷線了，重置標記
        if (s_wasConnected && !isConnected)
        {
            s_hasSentPlayerInfo = false;
            g_theGame->m_isOpponentConnected = false;
            g_theGame->m_opponentName = "";

            g_theDevConsole->AddLine(DevConsole::WARNING,
                "Disconnected from opponent");
        }

        s_wasConnected = isConnected;
    }
}

//----------------------------------------------------------------------------------------------------
void Game::Render() const
{
    PlayerController const* localPlayer = GetLocalPlayer(m_currentPlayerControllerId);

    //-Start-of-Game-Camera---------------------------------------------------------------------------

    g_theRenderer->BeginCamera(*localPlayer->GetCamera());

    if (m_gameState == eGameState::MATCH ||
        m_gameState == eGameState::FINISHED)
    {
        RenderEntities();
    }

    g_theRenderer->EndCamera(*localPlayer->GetCamera());

    //-End-of-Game-Camera-----------------------------------------------------------------------------
    //------------------------------------------------------------------------------------------------
    if (m_gameState == eGameState::MATCH)
    {
        // DebugAddWorldWireSphere(Vec3(2.f,2.f,2.f),0.01f, 0.f);
        DebugRenderWorld(*localPlayer->GetCamera());
    }
    //------------------------------------------------------------------------------------------------
    //-Start-of-Screen-Camera-------------------------------------------------------------------------

    g_theRenderer->BeginCamera(*m_screenCamera);

    if (m_gameState == eGameState::ATTRACT)
    {
        RenderAttractMode();
    }

    g_theRenderer->EndCamera(*m_screenCamera);

    //-End-of-Screen-Camera---------------------------------------------------------------------------

    DebugRenderScreen(*m_screenCamera);
}

void Game::TogglePlayerControllerId()
{
    if (m_currentPlayerControllerId == 0) m_currentPlayerControllerId = 1;
    else if (m_currentPlayerControllerId == 1) m_currentPlayerControllerId = 0;
}

bool Game::OnGameStateChanged(EventArgs& args)
{
    String const newGameState = args.GetValue("OnGameStateChanged", "DEFAULT");

    if (newGameState == "ATTRACT")
    {
        PieceDefinition::ClearAllDefs();
        BoardDefinition::ClearAllDefs();
        GAME_SAFE_RELEASE(g_theGame->m_match);
    }

    if (newGameState == "MATCH")
    {
        PieceDefinition::InitializeDefs("Data/Definitions/PieceDefinition.xml");
        BoardDefinition::InitializeDefs("Data/Definitions/BoardDefinition.xml");
        g_theGame->m_match = new Match();
        g_theEventSystem->FireEvent("OnMatchInitialized");
    }
    else if (newGameState == "FINISHED")
    {
        int const         id     = g_theGame->m_currentPlayerControllerId;
        PlayerController* player = g_theGame->GetLocalPlayer(id);
        player->m_position       = Vec3(9.5f, 4.f, 4.f);
        player->m_orientation    = EulerAngles(180, 45, 0);
    }

    return true;
}

//----------------------------------------------------------------------------------------------------
bool Game::Event_ChessConnect(EventArgs& args)
{
    if (!g_theNetworkSubsystem || !g_theGame)
        return false;

    std::string address = args.GetValue("address", "127.0.0.1");
    int port = args.GetValue("port", 3100);

    bool success = g_theNetworkSubsystem->ConnectToServer(address, port);

    if (success)
    {
        g_theDevConsole->AddLine(DevConsole::INFO_MAJOR,
            Stringf("Connecting to chess server at %s:%d", address.c_str(), port));
    }
    else
    {
        g_theDevConsole->AddLine(DevConsole::ERROR,
            "Failed to connect to server");
    }

    return true;
}

//----------------------------------------------------------------------------------------------------
bool Game::Event_ChessListen(EventArgs& args)
{
    if (!g_theNetworkSubsystem || !g_theGame)
        return false;

    int port = args.GetValue("port", 3100);

    bool success = g_theNetworkSubsystem->StartServer(port);

    if (success)
    {
        // 設定 Server 的玩家名稱
        std::string serverName = args.GetValue("name", "Host");
        g_theGame->m_playerName = serverName;

        g_theDevConsole->AddLine(DevConsole::INFO_MAJOR,
            Stringf("Server started on port %d", port));
        g_theDevConsole->AddLine(DevConsole::INFO_MAJOR,
            Stringf("Waiting for opponent..."));
    }
    else
    {
        g_theDevConsole->AddLine(DevConsole::ERROR,
            "Failed to start server");
    }

    return true;
}

//----------------------------------------------------------------------------------------------------
bool Game::Event_ChessPlayerInfo(EventArgs& args)
{
    if (!g_theNetworkSubsystem || !g_theGame)
        return false;

    std::string playerName = args.GetValue("name", "Unknown");

    // Server 收到 Client 的玩家資訊
    if (g_theNetworkSubsystem->IsServer())
    {
        // 儲存對手資訊
        g_theGame->m_opponentName = playerName;
        g_theGame->m_isOpponentConnected = true;

        g_theDevConsole->AddLine(DevConsole::INFO_MAJOR,
            Stringf("Player '%s' connected!", playerName.c_str()));

        // 重要：Server 必須回傳自己的玩家資訊給 Client
        std::string replyCommand = Stringf("ChessPlayerInfo name=%s",
            g_theGame->m_playerName.c_str());
        g_theNetworkSubsystem->SendRawData(replyCommand);

        g_theDevConsole->AddLine(DevConsole::INFO_MINOR,
            Stringf("Sent player info: %s", g_theGame->m_playerName.c_str()));

        // 如果有 Match 物件，更新它
        if (g_theGame->m_match)
        {
            // 這裡可能需要呼叫 Match 的某個函數來設定對手資訊
            // 例如：g_theGame->m_match->SetOpponent(playerName);
        }
    }
    // Client 收到 Server 的玩家資訊
    else if (g_theNetworkSubsystem->IsClient())
    {
        // 儲存對手資訊
        g_theGame->m_opponentName = playerName;
        g_theGame->m_isOpponentConnected = true;

        g_theDevConsole->AddLine(DevConsole::INFO_MAJOR,
            Stringf("Connected to host '%s'!", playerName.c_str()));

        // 如果有 Match 物件，更新它
        if (g_theGame->m_match)
        {
            // 這裡可能需要呼叫 Match 的某個函數來設定對手資訊
            // 例如：g_theGame->m_match->SetOpponent(playerName);
        }
    }

    return true;
}

eGameState Game::GetCurrentGameState() const
{
    return m_gameState;
}

int Game::GetCurrentPlayerControllerId() const
{
    return m_currentPlayerControllerId;
}

void Game::ChangeGameState(eGameState const newGameState)
{
    if (newGameState == m_gameState) return;

    EventArgs args;

    if (newGameState == eGameState::ATTRACT) args.SetValue("OnGameStateChanged", "ATTRACT");
    else if (newGameState == eGameState::MATCH) args.SetValue("OnGameStateChanged", "MATCH");
    else if (newGameState == eGameState::FINISHED) args.SetValue("OnGameStateChanged", "FINISHED");

    m_gameState = newGameState;

    g_theEventSystem->FireEvent("OnGameStateChanged", args);
}

bool Game::IsFixedCameraMode() const
{
    return m_isFixedCameraMode;
}

PlayerController* Game::GetCurrentPlayer()
{
    for (PlayerController* m_localPlayerController : m_localPlayerControllerList)
    {
        if (m_localPlayerController &&
            m_localPlayerController->GetControllerIndex() == m_currentPlayerControllerId)

            return m_localPlayerController;
    }

    return nullptr;
}

//----------------------------------------------------------------------------------------------------
void Game::UpdateFromInput()
{
    PlayerController const* localPlayer = GetLocalPlayer(m_currentPlayerControllerId);
    UNUSED(localPlayer)
    if (m_gameState == eGameState::ATTRACT)
    {
        if (g_theInput->WasKeyJustPressed(NUMCODE_0))
        {
            Window::s_mainWindow->SetWindowType(eWindowType::FULLSCREEN_CROP);
        }
        if (g_theInput->WasKeyJustPressed(NUMCODE_1))
        {
            Window::s_mainWindow->SetWindowType(eWindowType::WINDOWED);
        }
        if (g_theInput->WasKeyJustPressed(KEYCODE_ESC))
        {
            App::RequestQuit();
        }

        if (g_theInput->WasKeyJustPressed(KEYCODE_SPACE))
        {
            ChangeGameState(eGameState::MATCH);
        }
    }

    if (m_gameState == eGameState::MATCH ||
        m_gameState == eGameState::FINISHED)
    {
        if (g_theInput->WasKeyJustPressed(KEYCODE_ESC))
        {
            ChangeGameState(eGameState::ATTRACT);
        }

        if (g_theInput->WasKeyJustPressed(KEYCODE_P))
        {
            m_gameClock->TogglePause();
        }

        if (g_theInput->WasKeyJustPressed(KEYCODE_O))
        {
            m_gameClock->StepSingleFrame();
        }

        if (g_theInput->IsKeyDown(KEYCODE_T))
        {
            m_gameClock->SetTimeScale(0.1f);
        }

        if (g_theInput->WasKeyJustReleased(KEYCODE_T))
        {
            m_gameClock->SetTimeScale(1.f);
        }

        if (g_theInput->WasKeyJustPressed(KEYCODE_F6))
        {
            m_currentDebugInt = (m_currentDebugInt + (int)m_currentDebugIntRange.m_max) % (static_cast<int>(m_currentDebugIntRange.GetLength()) + 1);
        }
        if (g_theInput->WasKeyJustPressed(KEYCODE_F7))
        {
            m_currentDebugInt = (m_currentDebugInt + (int)m_currentDebugIntRange.m_min + 1) % (static_cast<int>(m_currentDebugIntRange.GetLength()) + 1);
        }

        g_theRenderer->SetPerFrameConstants((float)m_gameClock->GetTotalSeconds(), m_currentDebugInt, 0);

        DebugAddMessage(Stringf("DebugInt=%d|RenderMode=%s", m_currentDebugInt, GetDebugIntString(m_currentDebugInt)), 0.f, Rgba8::YELLOW);

        if (g_theInput->WasKeyJustPressed(KEYCODE_F4))
        {
            m_isFixedCameraMode = !m_isFixedCameraMode;

            for (int i = 0; i < static_cast<int>(m_localPlayerControllerList.size()); i++)
            {
                m_localPlayerControllerList[i]->SetControllerPosition(g_gameConfigBlackboard.GetValue(Stringf("playerControllerPosition%d", i), Vec3::ZERO));
                m_localPlayerControllerList[i]->SetControllerOrientation(g_gameConfigBlackboard.GetValue(Stringf("playerControllerOrientation%d", i), EulerAngles::ZERO));
                m_localPlayerControllerList[i]->m_worldCamera->SetPositionAndOrientation(m_localPlayerControllerList[i]->m_position, m_localPlayerControllerList[i]->m_orientation);
            }
        }

        // DebugAddMessage(Stringf("Use the DevConsole(~) to enter commands"), 0.f, Rgba8::YELLOW);
        String cameraMode = m_isFixedCameraMode ? "Fixed" : "Free";
        String gameState;
        if (m_currentPlayerControllerId == 0) gameState = "First player's turn.";
        else if (m_currentPlayerControllerId == 1) gameState = "Second player's turn.";
        DebugAddMessage(Stringf("CameraMode=%s|GameState=%s", cameraMode.c_str(), gameState.c_str()), 0.f, Rgba8::YELLOW);
        // DebugAddMessage(Stringf("Player Position: (%.2f, %.2f, %.2f)", localPlayer->m_position.x, localPlayer->m_position.y, localPlayer->m_position.z), 0.f);
    }
}


//----------------------------------------------------------------------------------------------------
void Game::UpdateEntities(float const gameDeltaSeconds, float const systemDeltaSeconds) const
{
    UNUSED(gameDeltaSeconds)
    if (m_match == nullptr) return;
    m_match->Update();
    GetLocalPlayer(m_currentPlayerControllerId)->Update(systemDeltaSeconds);
}

void Game::UpdateCurrentControllerId(int const newID)
{
    m_currentPlayerControllerId = newID;
}

//----------------------------------------------------------------------------------------------------
void Game::RenderAttractMode() const
{
    Vec2 clientDimensions = Window::s_mainWindow->GetViewportDimensions();

    VertexList_PCU verts;
    AddVertsForDisc2D(verts, Vec2((float)clientDimensions.x * 0.5f, (float)clientDimensions.y * 0.5f), 300.f, 10.f, Rgba8::YELLOW);
    g_theRenderer->SetModelConstants();
    g_theRenderer->SetBlendMode(eBlendMode::OPAQUE);
    g_theRenderer->SetRasterizerMode(eRasterizerMode::SOLID_CULL_BACK);
    g_theRenderer->SetSamplerMode(eSamplerMode::BILINEAR_CLAMP);
    g_theRenderer->SetDepthMode(eDepthMode::DISABLED);
    g_theRenderer->BindTexture(nullptr);
    g_theRenderer->BindShader(g_theRenderer->CreateOrGetShaderFromFile("Data/Shaders/Default"));
    g_theRenderer->DrawVertexArray(verts);

    std::vector<std::string> asciiArt = {
        "         ,....,",
        "      ,::::::<",
        "     ,::/^\"",
        "    ,::/, `   e`",
        "   ,::; |        '",
        "   ,::|  \\___,-.  c",
        "   ;::|     \\   ",
        "   ;::|      \\",
        "   ;::|   _.=`\\",
        "   `;:|.=` _.=`\\",
        "     '|_.=`   __\\",
        "     `\\_..==`` ",
        "      .'.___.-'.",
        "     /          \\",
        "    ('--......--')",
        "    /'--......--'\\",
        "    `\"--......--\""
    };

    Vec2  basePosition(480.f, 630.f);
    float lineHeight = 30.f;

    for (size_t i = 0; i < asciiArt.size(); ++i)
    {
        Vec2 position = basePosition - Vec2(0.f, i * lineHeight);
        DebugAddScreenText(Stringf(asciiArt[i].c_str()), position, lineHeight, Vec2(0.5f, 0.5f), 0.f);
    }
    DebugAddScreenText(Stringf("Chess Simulator"), Vec2(600.f, 60.f), lineHeight, Vec2(0.5f, 0.5f), 0.f);
}

//----------------------------------------------------------------------------------------------------
void Game::RenderEntities() const
{
    if (m_match == nullptr) return;
    PlayerController const* localPlayer = GetLocalPlayer(m_currentPlayerControllerId);

    m_match->Render();
    g_theRenderer->SetModelConstants(localPlayer->GetModelToWorldTransform());
    localPlayer->Render();
}

PlayerController* Game::CreateLocalPlayer(int const id)
{
    PlayerController* newPlayer = new PlayerController(nullptr);

    for (PlayerController const* controller : m_localPlayerControllerList)
    {
        if (controller && controller->GetControllerIndex() == id)
        {
            return nullptr;
        }
    }

    newPlayer->SetControllerIndex(id);
    newPlayer->SetControllerPosition(g_gameConfigBlackboard.GetValue(Stringf("playerControllerPosition%d", id), Vec3::ZERO));
    newPlayer->SetControllerOrientation(g_gameConfigBlackboard.GetValue(Stringf("playerControllerOrientation%d", id), EulerAngles::ZERO));
    newPlayer->m_worldCamera->SetPositionAndOrientation(newPlayer->m_position, newPlayer->m_orientation);
    m_localPlayerControllerList.push_back(newPlayer);

    return newPlayer;
}

//----------------------------------------------------------------------------------------------------
PlayerController* Game::GetLocalPlayer(int const id) const
{
    for (PlayerController* m_localPlayerController : m_localPlayerControllerList)
    {
        if (m_localPlayerController &&
            m_localPlayerController->GetControllerIndex() == id)

            return m_localPlayerController;
    }

    return nullptr;
}
