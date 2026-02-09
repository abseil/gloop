local include "devtools/blueprint/ncl/blueprint_file.ncl";

namespace gloop {
project_name = "gloop";
teams_product_id = 2022281534846;
local build_gardener = "gloop-eng+build-gardener@google.com";
continuous_build_email = ::blueprint::ContinuousBuildEmailInfo(
  build_cop_email_addrs = [build_gardener],
);
tech_lead = ["mkruskal"];
dev_mailing_list = "gloop-eng@google.com";
mdb_groups = ["gloop-eng-team"];
buganizer_component_ids = [2020424];
}  // namespace gloop
