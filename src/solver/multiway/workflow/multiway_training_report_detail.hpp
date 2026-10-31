#pragma once

#include "solver/multiway/workflow/multiway_training_report.hpp"

#include <iosfwd>

namespace texas::solver::multiway::training_report_detail
{
	void write_json(std::ostream& output, const MultiwayTrainingReport& report);
}
