/**
  \file maxPerf/App.h

  Sample application showing how to render simple graphics with maximum throughput and 
  minimum latency by stripping away most high level VFX and convenience features for
  development. This approach is good for some display and perception research. For general
  game and rendering applications, look at the G3D starter app and vrStarter which give very
  performance with a lot of high-level game engine features.

 */
#pragma once
#include <G3D/G3D.h>
#include "TargetEntity.h"
#include "ExperimentConfig.h"
#include "Experiment.h"
#include "Logger.h"
#include "PhysicsScene.h"
#include <chrono>

class FlyingEntity;
class JumpingEntity;

// An enum that tracks presentation state within a trial. Duration defined in experiment.h
// ready: ready scene that happens before beginning of a task.
// task: actual task (e.g. instant hit, tracking, projectile, ...)
// feedback: feedback showing whether task performance was successful or not.
enum PresentationState { initial, ready, task, feedback, scoreboard, complete };

class Projectile {
public:
	shared_ptr<VisibleEntity>       entity;
	/** When in hitscan mode */
	RealTime                        endTime;
	Projectile() : endTime(0) {}
	Projectile(const shared_ptr<VisibleEntity>& e, RealTime t = 0) : entity(e), endTime(t) {}
};

class FloatingCombatText : public VisibleEntity {
protected:
	shared_ptr<GFont> m_font;
	String m_text;
	float m_size;
	Color4 m_color;
	Color4 m_outline;
	Point3 m_offset;
	Point3 m_velocity;
	float m_fade;
	float m_timeout;
	RealTime m_created;

public:
	static shared_ptr<FloatingCombatText> create(String text, shared_ptr<GFont> font, float size, Color4 color, Color4 outlineColor, Point3 offset, Point3 velocity, float fade, float timeout_s) {
		return  createShared<FloatingCombatText>(text, font, size, color, outlineColor, offset, velocity, fade, timeout_s);
	}
	FloatingCombatText(String text, shared_ptr<GFont> font, float size, Color4 color, Color4 outlineColor, Point3 offset, Point3 velocity, float fade, float timeout_s) {
		m_text = text;
		m_font = font;
		m_size = size;
		m_color = color;
		m_outline = outlineColor;
		m_offset = offset;
		m_velocity = velocity;
		m_fade = fade;
		m_timeout = timeout_s;
		m_created = System::time();		// Capture the time at which this was created
	}

	bool draw(RenderDevice* rd, const Camera& camera, const Framebuffer& framebuffer) {
		// Abort if the timeout has expired (return false to remove this combat text from the tracked array)
		float time_existing = static_cast<float>(System::time() - m_created);
		if (time_existing > m_timeout) {
			return false;
		}

		// Project entity position into image space
		Rect2D viewport = Rect2D(framebuffer.vector2Bounds());
		Point3 position = camera.project(frame().translation, viewport);

		// Abort if the target is not in front of the camera 
		Vector3 diffVector = frame().translation - camera.frame().translation;
		if (camera.frame().lookRay().direction().dot(diffVector) < 0.0f) {
			return true;
		}
		// Abort if the target is not in the view frustum
		if (position == Point3::inf()) {
			return true;
		}
		position += m_offset;						// Apply (initial) offset in pixels
		position += time_existing * m_velocity;		// Update the position based on velocity

		// Apply the fade
		m_color.a *= m_fade;						
		m_outline.a *= m_fade;

		// Draw the font
		m_font->draw2D(rd, m_text, position.xy(), m_size, m_color, m_outline, GFont::XALIGN_CENTER, GFont::YALIGN_CENTER);
		return true;
	}
};

class App : public GApp {
protected:
	static const float TARGET_MODEL_ARRAY_SCALING;						///< Target model scale factor
	static const float TARGET_MODEL_ARRAY_OFFSET;						///< Target model offset
	static const int MAX_HISTORY_TIMING_FRAMES = 360;					///< Length of the history queue for m_frameDurationQueue
	const int                       numReticles = 55;					///< Total count of reticles available to choose from

	shared_ptr<ArticulatedModel>    m_viewModel;						///< Model for the weapon
	shared_ptr<Sound>               m_fireSound;						///< Sound for weapon firing
	shared_ptr<Sound>               m_explosionSound;					///< Sound for target exploding

	shared_ptr<GFont>				m_combatFont;						///< Font used for floating combat text
	Array<shared_ptr<FloatingCombatText>>	m_combatTextList;			///< Array of existing combat text

	shared_ptr<ArticulatedModel>    m_bulletModel;						///< Model for the "bullet"
	shared_ptr<ArticulatedModel>	m_decalModel;						///< Model for the miss decal
	shared_ptr<VisibleEntity>		m_lastDecal;						///< Model for the last decal we created
	shared_ptr<VisibleEntity>		m_firstDecal;						///< Model for the first decal we created
	shared_ptr<ArticulatedModel>	m_explosionModel;					///< Model for target destroyed animation
	shared_ptr<VisibleEntity>		m_explosion;						///< Model for target destroyed decal
	RealTime						m_explosionEndTime;					///< Time for end of explosion
		
	shared_ptr<PhysicsScene>		m_scene;							///< Pointer to the physics scene

	const int m_MatTableSize = 10;										///< Set this to set # of color "levels"
	Array<shared_ptr<UniversalMaterial>>	m_materials;				///< This stores the color materials

	GuiDropDownList*				m_sessDropDown;						///< Dropdown menu for session selection
	GuiDropDownList*				m_userDropDown;						///< Dropdown menu for user selection
	GuiLabel*						m_mouseDPILabel;					///< Label for mouse DPI field
	GuiLabel*						m_cm360Label;						///< Label for cm/360 field

	/** m_targetModelArray[10] is the base size. Away from that they get larger/smaller by TARGET_MODEL_ARRAY_SCALING */
	//Array<shared_ptr<ArticulatedModel>>  m_targetModelArray;			///< Array of various scaled target models
	Table<String, Array<shared_ptr<ArticulatedModel>>> m_targetModels;
	const int m_modelScaleCount = 20;

	/** Used for visualizing history of frame times. Temporary, awaiting a G3D built-in that does this directly with a texture. */
	Queue<float>                    m_frameDurationQueue;				///< Queue for history of frrame times

	/** Coordinate frame of the weapon, updated in onPose() */
	CFrame                          m_weaponFrame;						///< Frame for the weapon
	int                             m_displayLagFrames = 0;				///< Count of frames of latency to add

	/** Used to detect GUI changes to m_reticleIndex */
	int                             m_lastReticleLoaded = -1;			///< Last loaded reticle (used for change detection)
	int                             m_reticleIndex = numReticles;		///< Start by selecting the last reticle
	float                           m_sceneBrightness = 1.0f;			///< Scene brightness scale factor
	bool                            m_renderFPS = false;				///< Control flag used to draw (or not draw) FPS information to the display	
    GuiPane*                        m_currentUserPane;					///< Current user information pane

	// Drop down selection writebacks
	int								m_ddCurrentUser = 0;				///< Index of current user
	int								m_lastSeenUser = -1;				///< Index of last seen user (used for change determination)
	int								m_ddCurrentSession = 0;				///< Index of current session

	/** Projectile if false         */
	bool                            m_hitScan = true;					// NOTE: Projectile mode has not been implemented

	int                             m_lastUniqueID = 0;					///< Counter for creating unique names for various entities
	bool							m_sceneLoaded = false;				///< Indicates whether or not the scene has been loaded (prevents reload)
	
	// Hardware logger specific fields
	bool							m_loggerRunning = false;			///< Flag to indicate whether a python logger (for HW logger) is running (i.e. needs to be closed)
	HANDLE							m_loggerHandle = 0;					///< Process handle for the python logger instance (for HW logger) if running
	String							m_logName;							///< The log name used by the python logger instance (for HW logger) if running

	/** When m_displayLagFrames > 0, 3D frames are delayed in this queue */
	Array<shared_ptr<Framebuffer>>  m_ldrDelayBufferQueue;
	int                             m_currentDelayBufferIndex = 0;

    shared_ptr<GuiWindow>           m_userSettingsWindow;
    bool                            m_userSettingsMode = true;

	bool							m_crouched = false;

	/** Called from onInit */
	void makeGUI();
	void loadModels();
	void destroyTarget(int index);
	void updateUser(void);
    void updateUserGUI();

public:
	/* Moving from proctected so that Experiment classes can use it. */
	shared_ptr<GFont>               outputFont;						///< Font used for output
	shared_ptr<GFont>               hudFont;						///< Font used in HUD
	Array<shared_ptr<GFont>>		floatingCombatText;				///< Floating combat text array
	shared_ptr<Texture>             reticleTexture;					///< Texture used for reticle
	shared_ptr<Texture>             hudTexture;						///< Texture used for HUD
	bool                            emergencyTurbo = false;			///< Lower rendering quality to improve performance

	App(const GApp::Settings& settings = GApp::Settings());

	/** Array of all targets in the scene */
	Array<shared_ptr<TargetEntity>> targetArray;					///< Array of drawn targets
	Array<Projectile>                projectileArray;				///< Arrray of drawn projectiles

	int destroyedTargets = 0;							///< Number of targets destroyed


	/** Parameter configurations */
	UserTable						userTable;						///< Table of per user information (DPI/cm/360) that doesn't change across experiment
	UserStatusTable					userStatusTable;				///< Table of user status (session ordering/completed sessions) that do change across experiments
	ExperimentConfig                experimentConfig;				///< Configuration for the experiment and its sessions

	shared_ptr<Experiment> ex;										///< Pointer to the experiment
	shared_ptr<Logger> logger;										///< Pointer to the logger

	/** Call to change the reticle. */
	void setReticle(int r);

	/** Increment the current reticle index */
	void nextReticle() {
		setReticle((m_reticleIndex + 1) % (numReticles+1));
	}

    /** Creates a random target with motion based on parameters 
    @param motionDuration time in seconds to produce a motion path for
    @param motionDecisionPeriod time in seconds when new motion direction is chosen
    @param speed world-space velocity (m/s) of target
    @param radius world-space distance to target
    @param scale size of target TODO: is this radius or diameter in meters?*/
    void spawnParameterizedRandomTarget(float motionDuration, float motionDecisionPeriod, float speed, float radius, float scale);

	/** Creates a random target in front of the player */
	void spawnRandomTarget();

	/** Creates a spinning target */
	shared_ptr<FlyingEntity> spawnTarget(const Point3& position, float scale, bool spinLeft = true, const Color3& color = Color3::red(), String modelName= "model/target/target.obj");

	/** Creates a flying target */
	shared_ptr<FlyingEntity> spawnFlyingTarget(
		const Point3& position,
		float scale,
		const Color3& color,
		const Vector2& speedRange,
		const Vector2& motionChangePeriodRange,
		Point3 orbitCenter,
		String modelName,
		String name = ""
	);

	/** Creates a jumping target */
	shared_ptr<JumpingEntity> spawnJumpingTarget(
		const Point3& position,
		float scale,
		const Color3& color,
        const Vector2& speedRange,
        const Vector2& motionChangePeriodRange,
        const Vector2& jumpPeriodRange,
		const Vector2& distanceRange,
		const Vector2& jumpSpeedRange,
		const Vector2& gravityRange,
		Point3 orbitCenter,
		float targetDistance,
		String modelName,
		String name = ""
	);


    /** callback for saving user config */
	void userSaveButtonPress(void);
	/** used by experiment class to finish data logging */
	void mergeCurrentLogToCurrentDB();

	Array<String> updateSessionDropDown(void);

	String getDropDownSessId(void);

	String getDropDownUserId(void);

	void markSessComplete(String id);

	shared_ptr<UserConfig> getCurrUser(void);

	void updateSessionPress(void);

	void updateSession(String id);

	void runPythonLogger(String logName, String com, bool hasSync, String syncComPort);

	void killPythonLogger();
    void quitRequest();

	bool pythonMergeLogs(String basename);
    
	/** opens the user settings window */
    void openUserSettingsWindow();

	/** reads current user settings to update sensitivity in the controller */
    void updateMouseSensitivity();

	/** Fire the weapon - hits targets, draws decals, starts explosions */
	shared_ptr<TargetEntity> fire(bool destroyImmediately=false);

	/** clear all targets (used when clearing remaining targets at the end of a trial) */
	void clearTargets();

	/** get the current view direction */
	Point2 getViewDirection();
	
	virtual void onPostProcessHDR3DEffects(RenderDevice *rd) override;
	virtual void onInit() override;
	virtual void onAI() override;
	virtual void onNetwork() override;
	virtual void onSimulation(RealTime rdt, SimTime sdt, SimTime idt) override;
	virtual void onPose(Array<shared_ptr<Surface> >& posed3D, Array<shared_ptr<Surface2D> >& posed2D) override;
	virtual void onAfterLoadScene(const Any& any, const String& sceneName) override;
	virtual void onGraphics2D(RenderDevice* rd, Array<shared_ptr<Surface2D> >& surface2D) override;
	virtual void onGraphics3D(RenderDevice* rd, Array<shared_ptr<Surface> >& surface) override;
	virtual bool onEvent(const GEvent& e) override;
	virtual void onUserInput(UserInput* ui) override;
	virtual void onCleanup() override;
    virtual void oneFrame() override;
	
	// hardware setting
	struct ScreenSetting
	{
		float viewingDistance = 0.5f; // in m
		float screenDiagonal = 25.0f * 0.0254f; // in m (diagonal)
		Vector2 resolution = Vector2(1920, 1080);
		float pixelSize = screenDiagonal / sqrt(resolution.x * resolution.x + resolution.y * resolution.y);
		Vector2 screenSize = resolution * pixelSize;
	} m_screenSetting;

	//Color3                          m_reticleColor;
	bool							m_buttonUp = true;
};

// The "old" way of animation
/** Parameters related to animation during a trial. */
// Animation flow.
// updateAnimation() is called at the beginning of onGraphics3D. Workflow in updateLocation()
//       1. Check if trial time passed m_trialParam.trialDuration.
//                 If yes, end the current trial and initialize the next trial.
//       2. Check if motionChange is required.
//                 If yes, update m_rotationAxis. This is chosen among the vectors orthogonal to [camera - target] line.
//       3. update target location.
//       4. append to m_TargetArray an object with m_targetLocation.
