#pragma once

#include "Anatomy.h"
#include "Settings.h"

#include <array>
#include <optional>
#include "GeometryMath.h"
#include "fluid/Solver.h"
#include "fluid/Clusters.h"
#include "fluid/Surface.h"

namespace OStrands
{
	std::string TintTexturePath(const std::array<float, 3>& a_colour);   // 0.8.0.8: a flat-colour semen texture ("" = the default)

	class FluidManager
	{
	public:
		static FluidManager& Get();

		void SetOCum(bool a_installed);
		void SetSexLab(bool a_installed);
		void Climax(std::int32_t a_thread, RE::ActorHandle a_actor, std::vector<Pair> a_pairs, std::vector<RE::ActorHandle> a_actors);
		void Load(RE::ActorHandle a_actor, float a_fraction);
		void EndThread(std::int32_t a_thread);
		void Scene(std::int32_t a_thread, std::vector<RE::ActorHandle> a_actors);
		bool Inject(RE::ActorHandle a_host, std::uint32_t a_stream, RE::NiPoint3 a_point, RE::NiPoint3 a_velocity, int a_count,
			std::optional<std::array<float, 3>> a_colour, std::optional<std::array<float, 4>> a_glow = std::nullopt);
		static std::array<float, 4> GiverGlow(RE::Actor* a_giver, const std::array<float, 3>& a_colour);   // 0.8.0.7
		void Held(RE::ActorHandle a_receiver, RE::ActorHandle a_giver, int a_opening, float a_particles);   // 0.8.0.8: into a reservoir (precum)
		void Squeeze(RE::ActorHandle a_receiver, int a_opening);   // 0.8.0.8: a deep thrust into a full opening
		void Snowball(RE::ActorHandle a_one, RE::ActorHandle a_other);   // 0.8.0.8: a kiss - the fuller mouth gives half

		void Reset();
		void Update(float a_delta);
		void HideShapes();
		void WaitForBodyEditor(bool a_wait);
		bool SemenEnabled() const;
		bool SemenEnabled(std::int32_t a_thread) const;
		bool OCumInstalled() const { return _ocum; }
		bool SexLabInstalled() const { return _sexlab; }
		bool InScene(RE::ActorHandle a_actor) const;

		static constexpr std::size_t kRopeBones = 16;

	private:
		struct Event
		{
			enum class Type
			{
				kClimax,
				kLoad,
				kEnd,
				kScene,
				kInject,
				kHeld,    // 0.8.0.8: a little goes into an opening without a finish (precum)
				kSqueeze, // 0.8.0.8: a deep thrust into a full opening squeezes some out
				kSnowball // 0.8.0.8: a kiss passes cum from one mouth to the other
			};

			Type              type{ Type::kClimax };
			std::int32_t      thread{ 0 };
			RE::ActorHandle   actor;
			std::vector<Pair>            pairs;
			float                        value{ 0.0f };
			std::vector<RE::ActorHandle> actors;
			std::uint32_t                stream{ 0 };
			RE::NiPoint3                 point;
			RE::NiPoint3                 velocity;
			int                          count{ 0 };
			std::optional<std::array<float, 3>> colour;
			std::optional<std::array<float, 4>> glow;   // 0.8.0.7
		};

		struct Pulse
		{
			float at{ 0.0f };
			float speed{ 0.0f };
			float spacing{ 0.0f };
			int   particles{ 0 };
			float spread{ 0.0f };
			float angle{ 0.0f };
			float wave{ 0.0f };
			float frequency{ 0.0f };
		};

		struct Emitter
		{
			std::uint32_t       id{ 0 };
			std::int32_t        thread{ 0 };
			RE::ActorHandle     actor;
			std::optional<Pair> pair;
			std::vector<RE::ActorHandle> hosts;
			float               wait{ 0.0f };
			float               amount{ 1.0f };
			bool                loaded{ false };
			std::vector<Pulse>  pulses;
			std::size_t         pulse{ 0 };
			int                 emitted{ 0 };
			float               slot{ 0.0f };
			float               clock{ 0.0f };
			float               hang{ 0.0f };
			int                 count{ 0 };
			bool                oozing{ false };
			std::uint32_t       run{ 0 };
			std::uint32_t       domain{ 0 };
			RE::NiPoint3        lastNozzle;
			RE::NiPoint3        carried;
			bool                hasNozzle{ false };
			bool                inside{ false };
			bool                facial{ false };     // 0.8.0.8: pulled out of the mouth: spurts onto the face
			bool                noseRolled{ false }; // 0.8.0.8: the nose overflow was rolled for this finish
			bool                creature{ false };   // 0.8.0.4: a Colossal Load
			std::array<float, 3> colour{};
			std::array<float, 4> glow{};   // 0.8.0.7
			int                 planned{ 0 };
			SemenProfile        profile;             // 0.8.0.4: this giver's load (normal, Colossal, or Anatomica's)
			float               size{ 1.0f };        // 0.8.0.6: drawn this much bigger
			float               stick{ -1.0f };      // 0.8.0.6: its stickiness (-1 = [Semen] Stickiness)
			bool                heldIn{ false };     // 0.8.0.6: started inside an Anatomica anus - stays in while the tip is near
		};

		struct Blob
		{
			RE::NiPointer<RE::NiNode>          root;
			std::vector<RE::NiAVObject*>       bones;
			std::vector<Fluid::SurfacePoint>   surface;
			std::size_t                        seed{ 0 };
			bool                               live{ false };
			bool                               used{ false };
			std::string                        tint;
		};

		struct Rope
		{
			RE::NiPointer<RE::NiNode>                     root;
			std::array<RE::NiAVObject*, kRopeBones>       bones{};
			std::array<GeometryMath::Frame, kRopeBones>   frames{};
			bool                                          used{ false };
			std::string                                   tint;
		};

		struct Meshes
		{
			RE::NiPointer<RE::NiNode> blob;
			RE::NiPointer<RE::NiNode> rope;
			bool                      failed{ false };
		};

		struct Reservoir
		{
			std::uint32_t                id{ 0 };
			RE::ActorHandle              actor;
			Anatomy::Opening             opening{ Anatomy::Opening::kVagina };
			RE::ActorHandle              plug;
			std::vector<RE::ActorHandle> hosts;
			float                        volume{ 0.0f };
			float                        gush{ 0.0f };
			float                        release{ 0.0f };
			float                        age{ 0.0f };
			float                        dressed{ 0.0f };
			bool                         plugged{ true };
			std::uint32_t                domain{ 0 };
			std::uint32_t                run{ 0 };
			std::uint32_t                side{ 0 };   // 0.8.0.8: which corner of the mouth it dribbles from (turns about)
			bool                         swallowedAfter{ false };   // 0.8.0.8: out of the scene he swallowed most of what was left
			int                          glob{ 0 };
			float                        globSpeed{ 1.0f };
			float                        phase{ 0.0f };
			float                        period{ 2.0f };
			RE::NiPoint3                 lastExit;
			bool                         hasExit{ false };
		};

		struct Domain
		{
			std::uint32_t                  id{ 0 };
			RE::NiPoint3                   origin;
			RE::TESObjectCELL*             cell{ nullptr };
			bool                           broken{ false };
			std::vector<RE::ActorHandle>   hosts;
			std::vector<Fluid::Particle>   particles;
			std::vector<Fluid::Anchor>     anchors;
			std::vector<std::pair<RE::ActorHandle, Anatomy::Opening>> openings;
			std::unique_ptr<Fluid::Solver> solver;
			Fluid::DensityField            field;
			Fluid::CellGrid                clusterGrid;
			float                          accumulator{ 0.0f };
			RE::NiPointer<RE::NiNode>      root;
			std::vector<Blob>              blobs;
			std::vector<Rope>              ropes;
			float                          opacity{ -1.0f };
			float                          shine{ -1.0f };
			float                          gloss{ -1.0f };
			float                          age{ 0.0f };
			Fluid::Ground                  ground{ 0.08f };
			std::vector<Fluid::Capsule>    capsules;
			std::vector<Fluid::Hole>       holes;
			Fluid::Skin                    skin;
			std::size_t                    pooled{ 0 };
			std::size_t                    shownBlobs{ 0 };
			std::size_t                    shownRopes{ 0 };
			int                            spinning{ 0 };
			std::string                    spinSample;
			bool                           diagnostic{ false };
			bool                           incompleteReported{ false };
			bool                           creature{ false };
			bool                           custom{ false };   // 0.8.0.4: flows like the first load that coloured it
			struct MouthRun { RE::ActorHandle actor; float born{ 0.0f }; };
			std::unordered_map<std::uint32_t, MouthRun> mouthRuns;   // 0.8.0.8: dribbles from a mouth corner (let go at the jaw)
			SemenProfile                   profile;
			float                          size{ 1.0f };   // 0.8.0.6: globs and ropes drawn this much bigger
			float                          stick{ -1.0f }; // 0.8.0.6: its own stickiness (-1 = [Semen] Stickiness)
			bool                           coloured{ false };
			std::array<float, 3>           colour{};
			std::array<float, 4>           glow{};   // 0.8.0.7: emissive rgb + strength (0 = none)
			std::string                    tint;
		};

		void        Apply(Event& a_event);
		void        StartClimax(const Event& a_event);
		void        Plan(Emitter& a_emitter);
		bool        Step(Emitter& a_emitter, float a_delta);
		void        Deposit(const Emitter& a_emitter, float a_particles);
		bool        Leak(Reservoir& a_reservoir, float a_delta);
		Domain*     FindDomain(std::uint32_t a_id);
		static bool Holds(const Domain& a_domain, RE::Actor* a_actor, const RE::NiPoint3& a_point);
		Domain*     DomainFor(RE::Actor* a_actor, const RE::NiPoint3& a_point, const std::vector<RE::ActorHandle>& a_hosts);
		bool        Attach(Domain& a_domain);
		void        Simulate(Domain& a_domain, float a_delta);
		void        SampleGround(Domain& a_domain);
		void        ReleaseMouthRuns(Domain& a_domain);   // 0.8.0.8
		void        Expire(Domain& a_domain, float a_delta);
		void        Draw(Domain& a_domain);
		void        Watch(Domain& a_domain, const Fluid::Run& a_run);
		Meshes&     MeshesFor();
		RE::NiNode* CollisionMesh();
		Blob*       AcquireBlob(Domain& a_domain);
		bool        CloneBlob(Blob& a_blob);
		bool        CloneRope(Rope& a_rope, RE::NiNode* a_mesh);
		Rope*       AcquireRope(Domain& a_domain);
		void        PoseBlob(Domain& a_domain, Blob& a_blob);
		void        PoseDrop(Domain& a_domain, Blob& a_blob, Fluid::Vec3 a_center, float a_radius, Fluid::Vec3 a_velocity = {},
					std::optional<Fluid::Vec3> a_contact = std::nullopt);
		void        PoseRope(Domain& a_domain, Rope& a_rope, std::size_t a_begin, std::size_t a_end, const Fluid::Anchor* a_anchor);
		void        PoseCapsule(Rope& a_rope, const RE::NiPoint3& a_from, const RE::NiPoint3& a_to, float a_radius);
		void        Bend(Rope& a_rope, const std::array<RE::NiPoint3, kRopeBones>& a_positions, const std::array<float, kRopeBones>& a_radii);
		void        Reshade(Domain& a_domain);
		void        ShowShapes(float a_delta);
		void        Shade(Domain& a_domain, RE::NiAVObject* a_root);
		void        Tint(Domain& a_domain, RE::NiAVObject* a_root, std::string& a_applied);
		void        Colour(Domain& a_domain, bool a_creature, const std::array<float, 3>& a_colour, const std::array<float, 4>& a_glow = {});
		void        InjectNow(const Event& a_event);
		static std::array<float, 3> GiverColour(RE::Actor* a_giver, bool a_creature);
		void        Release(Domain& a_domain);
		Fluid::Vec3 ToLocal(const Domain& a_domain, const RE::NiPoint3& a_point) const;
		RE::NiPoint3 ToWorld(const Domain& a_domain, Fluid::Vec3 a_point) const;

		std::mutex         _lock;
		std::vector<Event> _pending;
		std::atomic<bool>  _ocum{ false };
		std::atomic<bool>  _sexlab{ false };

		std::vector<Emitter>                                  _emitters;
		std::vector<Reservoir>                                _reservoirs;
		std::vector<std::unique_ptr<Domain>>                  _domains;
		std::unique_ptr<Domain>                               _shapes;
		std::unordered_map<std::int32_t, std::vector<RE::ActorHandle>> _scenes;
		std::unordered_map<std::uint32_t, std::pair<float, float>> _loads;
		std::unordered_map<std::uint64_t, std::pair<std::uint32_t, float>> _streams;
		Meshes                                                _meshes;
		std::vector<Blob>                                     _spareBlobs;
		std::vector<Rope>                                     _spareRopes;
		int                                                   _cloneBudget{ 0 };
		RE::NiPointer<RE::NiNode>                             _collisionMesh;
		bool                                                   _collisionMeshFailed{ false };
		bool                                                   _debugAwaitingEditor{ false };
		bool                                                   _debugReady{ false };
		float                                                  _debugStableTime{}, _debugProbeTime{};
		std::vector<std::uint64_t>                             _debugMeshSignature;
		std::vector<Fluid::Vec3>                              _directions;
		std::vector<Fluid::SurfacePoint>                      _scratch;
		std::vector<Fluid::Particle>                          _ropePoints;
		std::vector<float>                                    _ropeAlong;
		std::uint32_t                                         _nextDomain{ 0 };
		std::uint32_t                                         _nextRun{ 0 };
		std::uint32_t                                         _nextEmitter{ 0 };
		std::mt19937                                          _rng{ std::random_device{}() };

		float  _statTime{ 0.0f };
		double _statSimulate{ 0.0 };
		double _statDraw{ 0.0 };
		double _statWorst{ 0.0 };
		int    _statClones{ 0 };
		int    _statFrames{ 0 };
	};
}
