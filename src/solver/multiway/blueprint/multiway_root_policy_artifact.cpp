#include "solver/multiway/blueprint/multiway_artifact.hpp"
#include "solver/multiway/blueprint/multiway_checkpoint.hpp"
#include "core/atomic_publish.hpp"
#include "core/fingerprint.hpp"
#include "core/portable_binary.hpp"

#include <array>
#include <fstream>
#include <stdexcept>

namespace texas::solver::multiway
{
	namespace
	{
		namespace portable = texas::core::portable;
		constexpr std::array<char, 8> kManifestMagic = { 'M', 'W', 'M', 'F', '0', '0', '0', '4' };
		using texas::core::fingerprint::append_u64;
		using texas::core::fingerprint::finish;
		void append_identity(std::uint64_t& hash, const MultiwayModelIdentity& identity) noexcept
		{
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
				append_u64(hash, field);
			});
		}
		void append_action(std::uint64_t& hash, const MultiwayActionDescriptor& action) noexcept
		{
			append_u64(hash, static_cast<std::uint64_t>(action.action));
			append_u64(hash, action.action_index);
			append_u64(hash, static_cast<std::uint64_t>(static_cast<std::int64_t>(action.target_street_contribution)));
			append_u64(hash, action.action_menu_id);
		}
		void write_identity(std::ofstream& out, const MultiwayModelIdentity& identity)
		{
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
				portable::write_u64(out, field);
			});
		}

		MultiwayModelIdentity read_identity(std::ifstream& in)
		{
			MultiwayModelIdentity identity;
			bool valid = true;
			visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) {
				if (valid)
					valid = portable::read_u64(in, field);
			});
			if (!valid)
			{
				throw std::runtime_error("multiway artifact is truncated");
			}
			return identity;
		}

		std::filesystem::path manifest_path(const std::filesystem::path& checkpoint_path)
		{
			return checkpoint_path.string() + ".manifest";
		}

		void save_manifest_atomic(const std::filesystem::path& path, const MultiwayBlueprintManifest& manifest)
		{
			const auto temp = path.string() + ".tmp";
			std::ofstream out(temp, std::ios::binary | std::ios::trunc);
			if (!out)
				throw std::runtime_error("multiway manifest temporary file cannot be opened");
			out.write(kManifestMagic.data(), static_cast<std::streamsize>(kManifestMagic.size()));
			portable::write_u32(out, manifest.schema_version);
			write_identity(out, manifest.identity);
			portable::write_u64(out, manifest.snapshot_hash);
			out.close();
			if (!out)
				throw std::runtime_error("multiway manifest write failed");
			texas::core::publish_atomic_replace(temp, path, "multiway manifest publish failed");
		}

		MultiwayBlueprintManifest load_manifest(const std::filesystem::path& path)
		{
			std::ifstream in(path, std::ios::binary);
			if (!in)
				throw std::runtime_error("multiway manifest cannot be opened");
			std::array<char, kManifestMagic.size()> magic{};
			in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
			if (!in || magic != kManifestMagic)
				throw std::runtime_error("multiway manifest schema is invalid");
			MultiwayBlueprintManifest manifest;
			if (!portable::read_u32(in, manifest.schema_version))
				throw std::runtime_error("multiway manifest is truncated");
			manifest.identity = read_identity(in);
			if (!portable::read_u64(in, manifest.snapshot_hash))
				throw std::runtime_error("multiway manifest is truncated");
			if (in.peek() != std::char_traits<char>::eof())
			{
				throw std::runtime_error("multiway manifest has trailing data");
			}
			manifest.validate();
			return manifest;
		}

	} // namespace

	void MultiwayBlueprintManifest::validate() const
	{
		if (schema_version != MULTIWAY_BLUEPRINT_MANIFEST_SCHEMA_VERSION || snapshot_hash == 0U)
		{
			throw std::invalid_argument("multiway blueprint manifest has invalid schema or hash");
		}
		identity.validate();
	}

	void MultiwayVerifiedBlueprintArtifact::validate(const MultiwayModelIdentity& expected_identity) const
	{
		snapshot.validate();
		manifest.validate();
		expected_identity.validate();
		if (snapshot.identity != manifest.identity || snapshot.identity != expected_identity || manifest.snapshot_hash != MultiwayBlueprintArtifacts::snapshot_hash(snapshot))
		{
			throw std::invalid_argument("multiway blueprint artifact identity or integrity mismatch");
		}
	}

	std::uint64_t MultiwayBlueprintArtifacts::snapshot_hash(const MultiwayBlueprintSnapshot& snapshot) noexcept
	{
		auto hash = texas::core::fingerprint::FNV1A_OFFSET;
		append_identity(hash, snapshot.identity);
		append_u64(hash, snapshot.public_state.value);
		append_u64(hash, snapshot.infoset.public_state.value);
		append_u64(hash, static_cast<std::uint64_t>(snapshot.infoset.seat));
		append_u64(hash, snapshot.bucket);
		append_u64(hash, snapshot.trajectories);
		append_u64(hash, static_cast<std::uint64_t>(snapshot.policy_kind));
		append_u64(hash, snapshot.training.batches);
		append_u64(hash, snapshot.training.trajectories);
		append_u64(hash, snapshot.training.deterministic_seed);
		append_u64(hash, snapshot.training.late_window_start_batch);
		append_u64(hash, snapshot.training.schedule_hash);
		append_u64(hash, snapshot.training.pruned_negative_regrets);
		append_u64(hash, snapshot.training.linear_iteration_weighting);
		append_u64(hash, snapshot.training.discounting_enabled);
		append_u64(hash, snapshot.training.negative_regret_pruning_enabled);
		append_u64(hash, snapshot.actions.size());
		for (const auto& action : snapshot.actions)
		{
			append_action(hash, action.action);
			append_u64(hash, action.probability);
		}
		return finish(hash);
	}

	void MultiwayBlueprintArtifacts::save_atomic(
		const std::filesystem::path& checkpoint_path,
		const MultiwayBlueprintSnapshot& snapshot)
	{
		snapshot.validate();
		MultiwayBlueprintManifest manifest;
		manifest.identity = snapshot.identity;
		manifest.snapshot_hash = snapshot_hash(snapshot);
		manifest.validate();
		MultiwayRootPolicyArtifact::save_atomic(checkpoint_path, snapshot);
		save_manifest_atomic(manifest_path(checkpoint_path), manifest);
	}

	MultiwayVerifiedBlueprintArtifact MultiwayBlueprintArtifacts::load_verified(
		const std::filesystem::path& checkpoint_path,
		const MultiwayModelIdentity& expected_identity)
	{
		MultiwayVerifiedBlueprintArtifact artifact;
		artifact.snapshot = MultiwayRootPolicyArtifact::load(checkpoint_path);
		artifact.manifest = load_manifest(manifest_path(checkpoint_path));
		artifact.validate(expected_identity);
		return artifact;
	}

	MultiwayVerifiedBlueprintArtifact MultiwayBlueprintArtifacts::load_with_fallback(
		const std::filesystem::path& primary_checkpoint_path,
		const std::filesystem::path& known_good_checkpoint_path,
		const MultiwayModelIdentity& expected_identity)
	{
		try
		{
			return load_verified(primary_checkpoint_path, expected_identity);
		}
		catch (const std::exception&)
		{
			auto fallback = load_verified(known_good_checkpoint_path, expected_identity);
			fallback.source = MultiwayArtifactSource::KnownGoodFallback;
			return fallback;
		}
	}

} // namespace texas::solver::multiway
