//----------------------------------------------------------------------------------------------------
// Piece.cpp
//----------------------------------------------------------------------------------------------------

//----------------------------------------------------------------------------------------------------
#include "Game/Gameplay/Piece.hpp"

#include "Engine/Core/VertexUtils.hpp"
#include "Engine/Math/AABB3.hpp"
#include "Engine/Math/Curve2D.hpp"
#include "Engine/Math/MathUtils.hpp"
#include "Engine/Renderer/BitmapFont.hpp"
#include "Engine/Renderer/Renderer.hpp"
#include "Game/Definition/BoardDefinition.hpp"
#include "Game/Definition/PieceDefinition.hpp"
#include "Game/Framework/GameCommon.hpp"
#include "Game/Gameplay/Match.hpp"
#include "ThirdParty/stb/stb_image.h"

//----------------------------------------------------------------------------------------------------
Piece::Piece(Match* owner, sSquareInfo const& squareInfo)
    : Actor(owner)
{
    m_definition = PieceDefinition::GetDefByName(squareInfo.m_name);

    m_shader                   = m_definition->m_shader;
    m_diffuseTexture           = m_definition->m_diffuseTexture;
    m_normalTexture            = m_definition->m_normalTexture;
    m_specularGlossEmitTexture = m_definition->m_specularGlossEmitTexture;
    m_coords                   = squareInfo.m_coords;
    m_id                       = squareInfo.m_playerControllerId;

    UpdatePositionByCoords(squareInfo.m_coords);
    // m_orientation = EulerAngles(45,0,0);
}

Vec3 Piece::CalculateKnightHopPosition(float t)
{
    // 將 3D 路徑分解為 XY 平面移動 + Z 軸跳躍

    // XY 平面：使用貝塞爾曲線創建弧形路徑
    Vec2 start2D = m_match->m_board->GetWorldPositionByCoords(m_startCoords).GetXY();
    Vec2 end2D   = m_match->m_board->GetWorldPositionByCoords(m_targetCoords).GetXY();

    // 創建弧形控制點（讓騎士走弧線而不是直線）
    Vec2 direction = end2D - start2D;
    Vec2 perpendicular(-direction.y, direction.x); // 垂直向量
    perpendicular = perpendicular.GetNormalized() * direction.GetLength() * 0.1f; // 弧度調整

    Vec2 guide1 = start2D + direction * 0.25f + perpendicular;
    Vec2 guide2 = start2D + direction * 0.75f + perpendicular;

    CubicBezierCurve2D horizontalCurve(start2D, guide1, guide2, end2D);
    Vec2               xyPosition = horizontalCurve.EvaluateAtParametric(t);

    // Z 軸：使用平滑函數創建跳躍高度
    float distance     = (m_match->m_board->GetWorldPositionByCoords(m_targetCoords) - m_match->m_board->GetWorldPositionByCoords(m_startCoords)).GetLength();
    float maxHopHeight = distance * 0.6f; // 跳躍高度

    // 使用 Hesitate3 或 SmoothStep3 創建拋物線效果
    float hopProgress = SmoothStep3(t); // 或 Hesitate3(t)
    float hopHeight   = maxHopHeight * (4.0f * hopProgress * (1.0f - hopProgress)); // 拋物線公式

    // 組合最終位置
    return Vec3(xyPosition.x, xyPosition.y,
                Interpolate(m_match->m_board->GetWorldPositionByCoords(m_startCoords).z, m_match->m_board->GetWorldPositionByCoords(m_targetCoords).z, t) + hopHeight);
}


//----------------------------------------------------------------------------------------------------
void Piece::Update(float const deltaSeconds)
{
    // Interpolation finishes no matter the value of FPS.
    m_orientation.m_yawDegrees += m_angularVelocity.m_yawDegrees * deltaSeconds;
    m_orientation.m_pitchDegrees += m_angularVelocity.m_pitchDegrees * deltaSeconds;
    m_orientation.m_rollDegrees += m_angularVelocity.m_rollDegrees * deltaSeconds;

    if (!m_isMoving) return;

    m_moveTimer += deltaSeconds;

    if (m_moveTimer >= m_moveDuration)
    {
        // 動畫完成
        m_position = m_match->m_board->GetWorldPositionByCoords(m_targetCoords);
        m_coords   = m_targetCoords;
        m_isMoving = false;
    }
    else
    {
        // 計算插值進度 (0.0 到 1.0)
        float t = m_moveTimer / m_moveDuration;

        if (m_definition->m_type == ePieceType::KNIGHT)
        {
            // 騎士跳躍動畫
            m_position = CalculateKnightHopPosition(t);
        }
        else
        {
            float smoothT = SmoothStep5(t); // 或 SmoothStep3, SmoothStep6
            m_position    = Interpolate(m_match->m_board->GetWorldPositionByCoords(m_coords), m_match->m_board->GetWorldPositionByCoords(m_targetCoords), smoothT);
        }
    }
}

//----------------------------------------------------------------------------------------------------
void Piece::Render() const
{
    g_theRenderer->SetModelConstants(GetModelToWorldTransform(), m_color);
    g_theRenderer->SetBlendMode(eBlendMode::OPAQUE);
    g_theRenderer->SetRasterizerMode(eRasterizerMode::SOLID_CULL_BACK);
    g_theRenderer->SetSamplerMode(eSamplerMode::POINT_CLAMP);
    g_theRenderer->SetDepthMode(eDepthMode::READ_WRITE_LESS_EQUAL);
    g_theRenderer->BindTexture(m_diffuseTexture, 0);
    g_theRenderer->BindTexture(m_normalTexture, 1);
    g_theRenderer->BindTexture(m_normalTexture, 1);
    g_theRenderer->BindTexture(m_specularGlossEmitTexture, 2);
    g_theRenderer->BindShader(m_shader);
    unsigned int const indexCount = m_definition->GetIndexCountByID(m_id);
    g_theRenderer->DrawIndexedVertexBuffer(m_definition->m_vertexBuffer[m_id], m_definition->m_indexBuffer[m_id], indexCount);

    if (m_isHighlighted||m_isSelected)
    {
        RenderSelectedPiece();
    }
    // RenderSelectedPiece();
    // RenderTargetPiece();
}

void Piece::RenderSelectedPiece() const
{
    VertexList_PCU verts;

    AddVertsForWireframeCylinder3D(verts, m_position, m_position+Vec3::Z_BASIS, 0.25f,0.005f);


g_theRenderer->SetModelConstants();
    g_theRenderer->BindTexture(nullptr);
    g_theRenderer->BindShader(g_theRenderer->CreateOrGetShaderFromFile("Data/Shaders/Default"));
    g_theRenderer->DrawVertexArray(verts);
}

void Piece::RenderTargetPiece() const
{
    g_theRenderer->SetModelConstants(GetModelToWorldTransform(), Rgba8(m_color.r, m_color.g, m_color.b,100));
    g_theRenderer->SetBlendMode(eBlendMode::ALPHA);
    g_theRenderer->SetRasterizerMode(eRasterizerMode::SOLID_CULL_BACK);
    g_theRenderer->SetSamplerMode(eSamplerMode::POINT_CLAMP);
    g_theRenderer->SetDepthMode(eDepthMode::READ_WRITE_LESS_EQUAL);
    g_theRenderer->BindTexture(m_diffuseTexture, 0);
    // g_theRenderer->BindTexture(m_normalTexture, 1);
    // g_theRenderer->BindTexture(m_normalTexture, 1);
    g_theRenderer->BindTexture(m_specularGlossEmitTexture, 2);
    g_theRenderer->BindShader(m_shader);
    unsigned int const indexCount = m_definition->GetIndexCountByID(m_id);
    g_theRenderer->DrawIndexedVertexBuffer(m_definition->m_vertexBuffer[m_id], m_definition->m_indexBuffer[m_id], indexCount);

}

void Piece::UpdatePositionByCoords(IntVec2 const& newCoords)
{
    m_position = m_match->m_board->GetWorldPositionByCoords(newCoords);
    m_coords   = newCoords;
}

void Piece::UpdatePositionByCoords(IntVec2 const& newCoords,
                                   float          moveTime)
{
    if (moveTime <= 0.0f)
    {
        // 立即移動
        m_position = m_match->m_board->GetWorldPositionByCoords(newCoords);
        m_coords   = newCoords;
        return;
    }

    // 設置動畫參數
    m_startCoords  = m_coords;
    m_targetCoords = newCoords;
    // m_targetPosition = m_match->m_board->GetWorldPositionByCoords(newCoords);
    m_targetCoords = newCoords;
    m_moveDuration = moveTime;
    m_moveTimer    = 0.0f;
    m_isMoving     = true;
}
