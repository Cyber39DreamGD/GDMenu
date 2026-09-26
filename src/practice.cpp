// Practice fix: GD doesn't restore the player's physics exactly when you respawn at a
// checkpoint, so a bot recorded in practice mode drifts slightly and dies when played
// back in one run. We snapshot the physics state when a checkpoint is placed and
// restore it bit-for-bit when you respawn there (same idea as xdBot / Eclipse practice fix).
#include "state.hpp"
#include <Geode/modify/PlayLayer.hpp>
#include <unordered_map>

// every plain (non-pointer, non-container) physics field on PlayerObject in 2.2081
#define GDM_PLAYER_FIELDS(X) \
	X(m_position) X(m_yVelocity) X(m_fallSpeed) X(m_isOnGround) X(m_isOnGround2) X(m_isOnGround3) \
	X(m_isOnGround4) X(m_isUpsideDown) X(m_platformerXVelocity) X(m_isDashing) X(m_vehicleSize) \
	X(m_playerSpeed) X(m_gravity) X(m_isOnSlope) X(m_wasOnSlope) X(m_lastLandTime) X(m_totalTime) \
	X(m_yVelocityBeforeSlope) X(m_isAccelerating) X(m_rotationSpeed) X(m_lastJumpTime) X(m_hasEverJumped) \
	X(m_isGoingLeft) X(m_shipRotation) X(m_isSideways) X(m_gravityMod) X(m_slopeVelocity) \
	X(m_speedMultiplier) X(m_isRotating) X(m_rotateSpeed) X(m_jumpBuffered) X(m_stateOnGround) \
	X(m_accelerationOrSpeed) X(m_touchedRing) X(m_touchedPad) X(m_dashX) X(m_dashY) X(m_dashAngle) \
	X(m_dashStartTime) X(m_lastPortalPos) X(m_lastGroundedPos) X(m_decreaseBoostSlide) X(m_isSliding) \
	X(m_isOnIce) X(m_isShip) X(m_isBird) X(m_isBall) X(m_isDart) X(m_isRobot) X(m_isSpider) X(m_isSwing) \
	X(m_fixGravityBug) X(m_reverseSync) X(m_isBallRotating) X(m_isBallRotating2) X(m_yStart) \
	X(m_ringJumpRelated) X(m_touchedCustomRing) X(m_touchedGravityPortal) X(m_wasJumpBuffered) \
	X(m_wasRobotJump) X(m_stateJumpBuffered) X(m_stateRingJump) X(m_stateRingJump2) X(m_slopeAngle) \
	X(m_slopeAngleRadians) X(m_isCollidingWithSlope) X(m_currentSlopeYVelocity) X(m_groundYVelocity) \
	X(m_lastFlipTime) X(m_physDeltaRelated) X(m_affectedByForces) X(m_stateForce) X(m_stateForceVector) \
	X(m_fixRobotJump) X(m_maybeIsFalling) X(m_padRingRelated) X(m_isLocked) X(m_platformerMovingLeft) \
	X(m_platformerMovingRight) X(m_leftPressedFirst) X(m_isMoving) X(m_playerSpeedAC) X(m_maybeIsBoosted) \
	X(m_changedDirectionsTime) X(m_isSlidingRight) X(m_fallStartY) X(m_snapDistance) X(m_slopeRotation) \
	X(m_stateScale) X(m_stateBoostX) X(m_stateBoostY) X(m_stateNoStickX) X(m_stateNoStickY) \
	X(m_stateHitHead) X(m_stateFlipGravity) X(m_stateDartSlide) X(m_stateNoAutoJump) X(m_yVelocityRelated) \
	X(m_yVelocityRelated3) X(m_platformerVelocityRelated) X(m_xVelocityRelated) X(m_xVelocityRelated2) \
	X(m_scaleXRelated) X(m_maybeSlopeForce) X(m_lastCheckpointTime)

namespace {
	struct PlayerSnapshot {
#define GDM_DECL(f) std::remove_reference_t<decltype(std::declval<PlayerObject&>().f)> f;
		GDM_PLAYER_FIELDS(GDM_DECL)
#undef GDM_DECL
		CCPoint nodePos;
		float rotation = 0.f;

		void save(PlayerObject* p) {
#define GDM_SAVE(f) f = p->f;
			GDM_PLAYER_FIELDS(GDM_SAVE)
#undef GDM_SAVE
			nodePos = p->getPosition();
			rotation = p->getRotation();
		}
		void apply(PlayerObject* p) const {
#define GDM_APPLY(f) p->f = f;
			GDM_PLAYER_FIELDS(GDM_APPLY)
#undef GDM_APPLY
			p->setPosition(nodePos);
			p->setRotation(rotation);
		}
	};

	struct CheckpointSnapshot {
		PlayerSnapshot p1, p2;
		bool dual = false;
	};

	std::unordered_map<CheckpointObject*, CheckpointSnapshot> s_snapshots;

	bool practiceFixActive() {
		return g_bot.state == BotState::Recording && Mod::get()->getSettingValue<bool>("practice-fix");
	}
}

class $modify(PracticeFixPlayLayer, PlayLayer) {
	bool init(GJGameLevel* level, bool useReplay, bool dontCreateObjects) {
		s_snapshots.clear();
		return PlayLayer::init(level, useReplay, dontCreateObjects);
	}

	// Eclipse does this too: never place a checkpoint on a dead player while recording
	CheckpointObject* markCheckpoint() {
		if (g_bot.state == BotState::Recording && (m_player1->m_isDead || (m_player2 && m_player2->m_isDead)))
			return nullptr;
		return PlayLayer::markCheckpoint();
	}

	void storeCheckpoint(CheckpointObject* cp) {
		PlayLayer::storeCheckpoint(cp);
		if (!cp) return;
		CheckpointSnapshot snap;
		snap.p1.save(m_player1);
		if (m_player2) snap.p2.save(m_player2);
		snap.dual = m_gameState.m_isDualMode;
		s_snapshots[cp] = snap;
	}

	void loadFromCheckpoint(CheckpointObject* cp) {
		PlayLayer::loadFromCheckpoint(cp);
		if (!practiceFixActive()) return;
		auto it = s_snapshots.find(cp);
		if (it == s_snapshots.end()) return;
		it->second.p1.apply(m_player1);
		if (m_player2 && it->second.dual) it->second.p2.apply(m_player2);
	}

	void resetLevelFromStart() {
		PlayLayer::resetLevelFromStart();
		s_snapshots.clear();
	}

	void onQuit() {
		s_snapshots.clear();
		PlayLayer::onQuit();
	}
};
