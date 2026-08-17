#include "schema.h"
#include <deque>
#include <string>

// shared leaves

static const Schema POINT3 = {
    {"x", 1, T::F64},
    {"y", 2, T::F64},
    {"z", 3, T::F64},
};

static const Schema POSE6 = {
    {"x", 1, T::F64},
    {"y", 2, T::F64},
    {"z", 3, T::F64},
    {"roll", 4, T::F64},
    {"pitch", 5, T::F64},
    {"yaw", 6, T::F64},
};

static const Schema INERTIA = {
    {"Ixx", 1, T::F64},
    {"Iyy", 2, T::F64},
    {"Izz", 3, T::F64},
    {"Ixy", 4, T::F64},
    {"Iyz", 5, T::F64},
    {"Ixz", 6, T::F64},
};

static const Schema FRIC = {
    {"coulomb_coef", 1, T::F64},
    {"viscous_coef", 2, T::F64},
    {"static_coef", 3, T::F64},
    {"motor_inertia", 4, T::F64},
    {"speed", 5, T::F64Arr, 6},
    {"upper_bound", 6, T::F64Arr, 6},
    {"lower_bound", 7, T::F64Arr, 6},
    {"fric_para_num", 8, T::U64S},
};

static const Schema ABILITY6 = {
    {"speed_turbo", 1, T::F64},
    {"speed_max", 2, T::F64},
    {"torque_turbo_acc", 3, T::F64},
    {"torque_max_acc", 4, T::F64},
    {"torque_turbo_dcc", 5, T::F64},
    {"torque_max_dcc", 6, T::F64},
};

static const Schema CS_ABILITY = {
    {"max_bending_torque", 1, T::F64},
    {"max_result_force", 2, T::F64},
    {"max_torque", 3, T::F64Arr, 3},
    {"max_force", 4, T::F64Arr, 3},
};

static const Schema SAFELIMIT = {
    {"Jmin", 1, T::F64Arr, 7},
    {"Jmax", 2, T::F64Arr, 7},
    {"Pmin", 3, T::F64Arr, 6},
    {"Pmax", 4, T::F64Arr, 6},
};

static const Schema POSE16 = {{"vertex", 1, T::F64Arr, 16}};

static const Schema JNT7 = {{"vertex", 1, T::F64Arr, 7}};

// ROBOT_CAPA

static const Schema ROBOT = {
    {"rob_type", 1, T::I32},
    {"axis_num", 2, T::I32},
    {"base_x_dir", 3, T::I32},
    {"base_z_dir", 4, T::I32},
    {"tcp_x_dir", 5, T::I32},
    {"tcp_z_dir", 6, T::I32},
    {"link_len", 7, T::F64Arr, 9},
    {"link_len_dyn", 8, T::F64Arr, 27},
    {"zero_ofs", 9, T::F64Arr, 7},
    {"axis_dir", 10, T::I32Arr, 7},
    {"mass", 11, T::F64Arr, 7},
    {"inertia", 12, T::Rep, &INERTIA, 7},
    {"mass_cent", 13, T::Rep, &POSE6, 7},
    {"fric_coef", 14, T::Rep, &FRIC, 7},
    {"jnt_stiff", 15, T::F64Arr, 7},
    {"link_stiff", 16, T::Rep, &POSE6, 7},
    {"cpl_ratio", 17, T::F64Arr, 49},
    {"stress_ability", 18, T::Rep, &ABILITY6, 7},
    {"stop_ability", 19, T::Rep, &ABILITY6, 7},
    {"thermal_ability", 20, T::Rep, &ABILITY6, 7},
    {"normal_cs_ability", 21, T::Rep, &CS_ABILITY, 7},
    {"stop_cs_ability", 22, T::Rep, &CS_ABILITY, 7},
    {"ident_type", 23, T::I32},
    {"ident_param_num", 24, T::I32},
    {"ident_param", 25, T::F64Arr, 100},
    {"cutoff_freq", 26, T::F64},
    {"res", 27, T::F64Arr, 10},
};

static const Schema CVX = {
    {"type", 1, T::I32},
    {"poseT", 2, T::Msg, &POSE16},
    {"x", 3, T::F64},
    {"y", 4, T::F64},
    {"z", 5, T::F64},
    {"vertex", 6, T::Rep, &POINT3, 64},
    {"verNum", 7, T::I32},
    {"boundXn", 8, T::F64},
    {"boundXp", 9, T::F64},
    {"boundYn", 10, T::F64},
    {"boundYp", 11, T::F64},
    {"boundZn", 12, T::F64},
    {"boundZp", 13, T::F64},
};

static const Schema VIBSUP = {
    {"install_mode", 1, T::I32},
    {"res1", 2, T::I32},
    {"full_load_kg", 3, T::F64},
    {"lower_bound_jnt2", 4, T::F64},
    {"lower_bound_jnt3", 5, T::F64},
    {"grid_size_jnt2", 6, T::F64},
    {"grid_size_jnt3", 7, T::F64},
    {"grid_num_jnt2", 8, T::F64},
    {"grid_num_jnt3", 9, T::F64},
    {"stiff_tab_K1_zeroLoad", 10, T::F64Arr, 1225},
    {"stiff_tab_K2_zeroLoad", 11, T::F64Arr, 1225},
    {"stiff_tab_K1_halfLoad", 12, T::F64Arr, 1225},
    {"stiff_tab_K2_halfLoad", 13, T::F64Arr, 1225},
    {"stiff_tab_K1_fullLoad", 14, T::F64Arr, 1225},
    {"stiff_tab_K2_fullLoad", 15, T::F64Arr, 1225},
    {"stiff_K3", 16, T::F64},
    {"res", 17, T::F64Arr, 4},
};

static const Schema RCU_ABT = {
    {"Wmax", 1, T::F64Arr, 7},
    {"WAcc", 2, T::F64Arr, 7},
    {"WDec", 3, T::F64Arr, 7},
    {"WJmax", 4, T::F64Arr, 7},
    {"Tmax", 5, T::F64Arr, 7},
    {"Vmax", 6, T::F64Arr, 6},
    {"VAcc", 7, T::F64Arr, 6},
    {"VDec", 8, T::F64Arr, 6},
    {"VJmax", 9, T::F64Arr, 6},
    {"Fmax", 10, T::F64Arr, 6},
};

static const Schema ABILITY = {
    {"rcu_abt", 1, T::Msg, &RCU_ABT},
    {"hand_high_ability", 2, T::F64},
    {"hand_low_ability", 3, T::F64},
    {"auto_ability", 4, T::F64},
    {"safelimit", 5, T::Msg, &SAFELIMIT},
};

static const Schema JOG = {
    {"recall_time", 1, T::U32},
    {"Vmax", 2, T::F64Arr, 6},
    {"Amax", 3, T::F64Arr, 6},
    {"Wmax", 4, T::F64Arr, 7},
    {"WAcc", 5, T::F64Arr, 7},
    {"ability_percent", 6, T::F64},
};

static const Schema SMV = {
    {"K1", 1, T::F64Arr, 7},
    {"K2", 2, T::F64Arr, 7},
    {"K3", 3, T::F64Arr, 7},
    {"K4", 4, T::F64Arr, 7},
    {"K5", 5, T::F64Arr, 7},
    {"K6", 6, T::F64Arr, 7},
    {"K7", 7, T::I32Arr, 7},
    {"K8", 8, T::I32Arr, 7},
};

static const Schema SERVO = {
    {"zero_pulse", 1, T::I32},
    {"motor_ratio", 2, T::F64},
    {"motor_encoder", 3, T::U64S},
    {"motor_eczero", 4, T::I32},
    {"motor_speed", 5, T::F64},
    {"motor_torque", 6, T::F64},
    {"speed_ep", 7, T::I32},
    {"motor_ecurrent", 8, T::F64},
    {"stop_time", 9, T::F64},
};

static const Schema MAINT = {
    {"item_num", 1, T::I32},
    {"item_ids", 2, T::I32Arr, 20},
    {"life_time", 3, T::I32Arr, 20},
};

static const Schema DEVICE_INFO = {
    {"product_type", 1, T::Str},
    {"servo_param_model", 2, T::I32},
    {"prod_date", 3, T::Str},
    {"write_date", 4, T::Str},
    {"controller_type", 5, T::I32},
    {"serial_num", 6, T::Str},
    {"hw_profile", 7, T::I32},
};

static const Schema NIC_INFO = {
    {"nic_name", 1, T::Str},
    {"nic_ip", 2, T::Str},
    {"gateway", 3, T::Str},
    {"nic_mode", 4, T::U32},
};

// field 2 is unused on the wire -- numbers are NOT dense here

static const Schema NIC_CONFIG = {
    {"nic_info", 1, T::Rep, &NIC_INFO, 8},
    {"valid_nic_num", 3, T::I32},
};

// Build the known sys_limit scalar names at load time.
// Note the vendor's spelling "HightAbility" -- reproduce the typo.

static const Schema& sysLimit()
{
    static const Schema s = []
    {
        Schema v;
        // deque, not vector: element addresses must stay valid as this grows.
        // because each field holds a raw char* into these strings.
        static std::deque<std::string> names;
        uint32_t n = 1;
        auto add = [&](const std::string& k, T t)
        {
            names.push_back(k);
            v.push_back(Field(names.back().c_str(), n++, t));
        };

        for (const char* a : {"LowAbility", "HightAbility", "AutoAbility", "joint", "Pose"})
        {
            add(std::string("Jog") + a + "Min", T::F64);
            add(std::string("Jog") + a + "Dft", T::F64);
            add(std::string("Jog") + a + "Max", T::F64);
        }

        for (const char* k :
             {"CDLevelMin", "CDLevelDft", "CDLevelMax", "SDLevelMin", "SDLevelDft", "SDLevelMax"})
        {
            add(k, T::U32);
        }
        for (int j = 1; j <= 7; ++j)
        {
            add("SLJ" + std::to_string(j) + "min", T::F64);
            add("SLJ" + std::to_string(j) + "max", T::F64);
        }
        for (const char* k :
             {"ToolLoadMax", "RollMin", "RollMax", "PitchMin", "PitchMax", "YawMin", "YawMax"})
        {
            add(k, T::F64);
        }
        return v;
    }();
    return s;
}

static const Schema ROBOT_CAPA = {
    {"device_info", 1, T::Msg, &DEVICE_INFO},
    {"nic_config", 2, T::Msg, &NIC_CONFIG},
    {"robot", 3, T::Msg, &ROBOT},
    {"cvx", 4, T::Rep, &CVX, 9},
    {"vib_sup_param", 5, T::Msg, &VIBSUP},
    {"ability", 6, T::Msg, &ABILITY},
    {"jog_param", 7, T::Msg, &JOG},
    {"smv_param", 8, T::Msg, &SMV},
    {"servo_param", 9, T::Rep, &SERVO, 7},
    {"factory_pose", 10, T::F64Arr, 7},
    {"rbt_maintance_info", 11, T::Msg, &MAINT}, // vendor's typo
    {"sys_limit", 12, T::Msg, &sysLimit()},
};

// RIU_CALIB_PARA

static const Schema KNMP = {
    {"flag", 1, T::I32},
    {"res", 2, T::I32},
    {"zero_pulse_pre", 3, T::I32Arr, 7},
    {"zero_pulse_post", 4, T::I32Arr, 7},
    {"link_len0", 5, T::F64Arr, 9},
    {"zero_modify0", 6, T::F64Arr, 7},
    {"link_len", 7, T::F64Arr, 9},
    {"back_err", 8, T::F64Arr, 7},
    {"rdc_ratio", 9, T::F64Arr, 7},
    {"cpl_ratio", 10, T::F64Arr, 49},
    {"rdc_err", 11, T::F64Arr, 7},
    {"cpl_err", 12, T::F64Arr, 49},
    {"zero_modify", 13, T::F64Arr, 7},
    {"jnt_stiff", 14, T::F64Arr, 7},
    {"knmp", 15, T::F64Arr, 256},
    {"knmp_num", 16, T::I32},
    {"res2", 17, T::I32},
};

static const Schema SHAPE = {
    {"type", 1, T::I32},
    {"poseT", 2, T::Msg, &POSE16},
    {"x", 3, T::F64},
    {"y", 4, T::F64},
    {"z", 5, T::F64},
    {"vertex", 6, T::Rep, &POINT3, 64},
    {"verNum", 7, T::I32},
    {"boundXn", 8, T::F64},
    {"boundXp", 9, T::F64},
    {"boundYn", 10, T::F64},
    {"boundYp", 11, T::F64},
    {"boundZn", 12, T::F64},
    {"boundZp", 13, T::F64},
};

static const Schema BODY = {
    {"shape", 1, T::Msg, &SHAPE},
    {"poseT", 2, T::Msg, &POSE16},
    {"mass", 3, T::F64},
    {"inertia", 4, T::Msg, &INERTIA},
    {"massCenter", 5, T::Msg, &POSE6},
};

static const Schema TOOL = {
    {"body", 1, T::Msg, &BODY},
    {"cvxEnable", 2, T::I32},
    {"type", 3, T::I32},
    {"enable", 4, T::I32},
};

static const Schema KNM_RAW = {
    {"tool", 1, T::Msg, &TOOL},
    {"num_knm", 2, T::I32},
    {"num_knm2", 3, T::I32},
    {"jnt", 4, T::Rep, &JNT7, 200},
    {"pos_meas", 5, T::Rep, &POSE6, 200},
};

static const Schema DYNP = {
    {"flag", 1, T::I32},
    {"res", 2, T::I32},
    {"tool", 3, T::Msg, &TOOL},
    {"obj", 4, T::Msg, &TOOL},
    {"mass", 5, T::F64Arr, 7},
    {"inertia", 6, T::Rep, &INERTIA, 7},
    {"mass_cent", 7, T::Rep, &POSE6, 7},
    {"dyn_param", 8, T::F64Arr, 100},
    {"coulomb_fric", 9, T::F64Arr, 7},
    {"viscous_fric", 10, T::F64Arr, 7},
    {"dyn_param_num", 11, T::I32},
    {"dynp", 12, T::F64Arr, 256},
    {"dynp_num", 13, T::I32},
    {"res1", 14, T::I32},
};

static const Schema RIU_CALIB_PARA = {
    {"knmp", 1, T::Msg, &KNMP},
    {"knm_raw_data", 2, T::Msg, &KNM_RAW},
    {"dynp", 3, T::Msg, &DYNP},
    {"is_knm_valid", 4, T::I32},
    {"is_dyn_valid", 5, T::I32},
    {"is_encode_zero_valid", 6, T::I32},
    {"encode_zero", 7, T::I32Arr, 7},
};

// ROBOT_CONFIG

static const Schema MOD = {
    {"cd_level", 1, T::I32},
    {"sap_flg", 2, T::I32},
    {"spd_flg", 3, T::I32},
    {"sd_level", 4, T::I32},
    {"vib_level", 5, T::I32},
    {"stiff_level", 6, T::I32},
    {"filter_level", 7, T::I32},
    {"res", 8, T::I32Arr, 2},
};

static const Schema SHAPE_E = {
    {"type", 1, T::I32},
    {"poseE", 2, T::Msg, &POSE6},
    {"lx", 3, T::F64},
    {"ly", 4, T::F64},
    {"lz", 5, T::F64},
    {"vertex", 6, T::Rep, &POINT3, 64},
    {"verNum", 7, T::I32},
    {"boundXn", 8, T::F64},
    {"boundXp", 9, T::F64},
    {"boundYn", 10, T::F64},
    {"boundYp", 11, T::F64},
    {"boundZn", 12, T::F64},
    {"boundZp", 13, T::F64},
};

static const Schema BODY_E = {
    {"shape", 1, T::Msg, &SHAPE_E},
    {"poseE", 2, T::Msg, &POSE6},
    {"mass", 3, T::F64},
    {"inertia", 4, T::Msg, &INERTIA},
    {"massCenter", 5, T::Msg, &POSE6},
};

static const Schema EXBODY = {
    {"axisID", 1, T::I32},
    {"body", 2, T::Msg, &BODY_E},
    {"cvxEnable", 3, T::I32},
    {"enable", 4, T::I32},
};

static const Schema EXBODYS = {
    {"num", 1, T::I32},
    {"exbody", 2, T::Rep, &EXBODY, 10},
};

static const Schema EXAXIS_PARAM = {
    {"type", 1, T::I32},
    {"idx", 2, T::I32},
    {"dep_idx", 3, T::I32},
    {"flag_connect", 4, T::I32},
    {"frame", 5, T::Msg, &POSE6},
};

static const Schema CS_SELECT = {
    {"csType", 1, T::I32},
    {"csID", 2, T::I32},
};

static const Schema CFG_JOG = {
    {"dj", 1, T::F64},
    {"dp", 2, T::F64},
    {"type", 3, T::I32},
};

static const Schema STOP3_CFG = {
    {"enable", 1, T::I32},
    {"max_back_dis", 2, T::I32},
};

static const Schema CONFIG = {
    {"is_safelimit_valid", 1, T::I32},
    {"safelimit", 2, T::Msg, &SAFELIMIT},
    {"mod", 3, T::Msg, &MOD},
    {"gravity", 4, T::Msg, &POINT3},
    {"exbodys", 5, T::Msg, &EXBODYS},
    {"exaxis_param", 6, T::Msg, &EXAXIS_PARAM},
    {"tool_select", 7, T::I32},
    {"csSelect", 8, T::Msg, &CS_SELECT},
    {"jog_param", 9, T::Msg, &CFG_JOG},
    {"wobjID", 10, T::U32},
    {"installType", 11, T::I32},
    {"stop3_cfg", 12, T::Msg, &STOP3_CFG},
};

// wire field 9 (NIC device name, e.g. "end0") is parsed but never emitted.

static const Schema NIC = {
    {"enable_protocol", 1, T::I32},
    {"protocol_type", 2, T::I32},
    {"mode", 3, T::U32},
    {"ip", 4, T::Str},
    {"mask", 5, T::Str},
    {"gate", 6, T::Str},
    {"nic_idx", 7, T::I32},
};

static const Schema NET_CFG = {
    {"config_nic_num", 1, T::U32},
    {"nic", 2, T::Rep, &NIC, 8},
};

static const Schema PN_SLAVE = {
    {"key", 1, T::I32},
    {"nic_idx", 2, T::I32},
    {"valid_bytes", 3, T::I32},
    {"remote_endian", 4, T::I32},
    {"desp_name", 5, T::Str},
    {"ip", 6, T::Str},
    {"gsd_file_name", 7, T::Str},
};

static const Schema PN_MASTER = {
    {"key", 1, T::I32},
    {"desp_name", 2, T::Str},
    {"slave_num", 3, T::I32},
    {"nic_idx", 4, T::I32},
    {"slaves", 5, T::Rep, &PN_SLAVE, 8},
};

static const Schema EIP_SLAVE = {
    {"key", 1, T::I32},
    {"nic_idx", 2, T::I32},
    {"valid_bytes", 3, T::I32},
    {"remote_endian", 4, T::I32},
    {"desp_name", 5, T::Str},
    {"ip", 6, T::Str},
    {"port", 7, T::U32},
    {"eds_file_name", 8, T::Str},
    {"connection_path", 9, T::Str},
    {"vendor_id", 10, T::I32},
    {"rpi", 11, T::I32},
    {"send_len", 12, T::I32},
};

static const Schema EIP_MASTER = {
    {"key", 1, T::I32},
    {"desp_name", 2, T::Str},
    {"slave_num", 3, T::I32},
    {"nic_idx", 4, T::I32},
    {"slaves", 5, T::Rep, &EIP_SLAVE, 8},
    {"master_ip", 6, T::Str},
    {"remote_endian", 7, T::U32},
};

static const Schema MB_SLAVE = {
    {"key", 1, T::I32},
    {"nic_idx", 2, T::I32},
    {"modbus_id", 3, T::I32},
    {"ip", 4, T::Str},
    {"port", 5, T::U32},
    {"remote_endian", 6, T::U32},
    {"start_addr", 7, T::Str},
    {"desp_name", 9, T::Str},
    {"reg_num", 10, T::I32},
};

static const Schema MB_MASTER = {
    {"key", 1, T::I32},
    {"desp_name", 2, T::Str},
    {"slave_num", 3, T::I32},
    {"nic_idx", 4, T::I32},
    {"slaves", 5, T::Rep, &MB_SLAVE, 4},
};

static const Schema WELD = {
    {"weld_type", 1, T::Str},
    {"enable", 2, T::I32},
    {"sim_mode", 3, T::I32},
    {"voltage_mode", 4, T::I32},
};

static const Schema CONTROL_PARAM = {
    {"working_mode", 1, T::I32},
    {"run_cnt", 2, T::I32},
    {"cur_project", 3, T::Str},
    {"network_config", 4, T::Msg, &NET_CFG},
    {"hand_high_ability", 5, T::F64},
    {"hand_low_ability", 6, T::F64},
    {"auto_ability", 7, T::F64},
    {"home_pose", 10, T::F64Arr, 7},
    {"is_encode_zero_valid", 11, T::I32},
    {"encode_zero", 12, T::I32Arr, 7},
    {"enable_network_protocol", 13, T::I32},
    {"network_type", 14, T::I32},
    {"network_ip", 15, T::Str},
    {"network_port", 16, T::I32},
    // Fields 24/25 exist here, BEFORE field 17 -- this is the vendor's order --
    // it looks like a mistake; do not "fix" it into numeric order.
    {"is_ex_axis_encode_zero_valid", 24, T::I32},
    {"ex_axis_encode_zero", 25, T::I32Arr, 7},
    {"pn_master_config", 17, T::Rep, &PN_MASTER, 8},
    {"pn_slave_config", 18, T::Rep, &PN_SLAVE, 8},
    {"eip_master_config", 19, T::Rep, &EIP_MASTER, 8},
    {"eip_slave_config", 20, T::Rep, &EIP_SLAVE, 8},
    {"modbus_master_config", 21, T::Rep, &MB_MASTER, 8},
    {"modbus_slave_config", 22, T::Rep, &MB_SLAVE, 8},
    {"weld_config", 31, T::Msg, &WELD},
};

static const Schema CUSTOM_BUTTON = {
    {"button_name", 1, T::Str},
    {"function", 2, T::U32},
};

static const Schema PENDANT = {
    {"pendant_language", 1, T::Str},
    {"custom_button", 2, T::Msg, &CUSTOM_BUTTON},
    {"sleep_time", 3, T::I32},
    {"bright_config", 4, T::I32},
};

static const Schema ITEM_INFO = {
    {"item_id", 1, T::U32},
    {"used_time", 2, T::U32},
    {"life_time", 3, T::U32},
    {"year", 4, T::I32},
    {"month", 5, T::I32},
    {"day", 6, T::I32},
    {"last_maintance_year", 7, T::I32},
    {"last_maintance_month", 8, T::I32},
    {"last_maintance_day", 9, T::I32},
    {"is_alarm", 10, T::I32},
};

static const Schema OMI = {
    {"item_num", 1, T::I32},
    {"last_record_timestamp", 2, T::U64S},
    {"item_info", 3, T::Rep, &ITEM_INFO, 20},
};

static const Schema EX_PARAM = {
    {"dir", 1, T::I32},
    {"motor_ratio", 2, T::F64},
    {"motor_encoder", 3, T::U64S},
    {"type", 4, T::I32},
    {"idx", 5, T::I32},
    {"dep_idx", 6, T::I32},
    {"flag_connect", 7, T::I32},
    {"frame", 8, T::Msg, &POSE6},
    {"VWmax", 9, T::F64},
    {"VWAcc", 10, T::F64},
    {"VWDec", 11, T::F64},
    {"VWJmax", 12, T::F64},
    {"JPMin", 13, T::F64},
    {"JPMax", 14, T::F64},
};

static const Schema EX_AXIS_CFG = {
    {"ex_axis_num", 1, T::I32},
    {"exParam", 2, T::Rep, &EX_PARAM, 7},
};

static const Schema ROBOT_CONFIG = {
    {"config", 1, T::Msg, &CONFIG},
    {"control_param", 2, T::Msg, &CONTROL_PARAM},
    {"pendant_param", 3, T::Msg, &PENDANT},
    {"operate_maintenance_info", 4, T::Msg, &OMI},
    {"ex_axis_cfg", 5, T::Msg, &EX_AXIS_CFG},
};

// registry

const Schema* lookupTable(const std::string& name)
{
    if (name == "ROBOT_CAPA")
        return &ROBOT_CAPA;

    if (name == "RIU_CALIB_PARA")
        return &RIU_CALIB_PARA;

    if (name == "ROBOT_CONFIG")
        return &ROBOT_CONFIG;

    return nullptr;
}
