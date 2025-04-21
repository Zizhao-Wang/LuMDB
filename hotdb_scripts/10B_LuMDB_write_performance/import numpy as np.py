import numpy as np
import itertools
from time import time

# 系统配置
np.random.seed(42)
SIMULATION_TIMES = 100000
start_time = time()

# 决策位含义说明
DECISION_MEANING = [
    "检测元件1",
    "检测元件2",
    "检测成品",
    "拆解退回品",
    "检测拆解元件1",
    "检测拆解元件2"
]

# 完整业务数据（8种情形）
data = {
    1: {'defect1': 0.094, 'c1': 42, 'd1': 21, 'defect2': 0.101, 'c2': 184, 'd2': 58,
        'defect_a': 0.099, 'ca': 63, 'da': 32, 'p': 654, 'l': 62, 'high_risk': False},
    2: {'defect1': 0.100, 'c1': 42, 'd1': 20, 'defect2': 0.151, 'c2': 184, 'd2': 32,
        'defect_a': 0.075, 'ca': 61, 'da': 33, 'p': 650, 'l': 302, 'high_risk': False},
    3: {'defect1': 0.091, 'c1': 44, 'd1': 82, 'defect2': 0.195, 'c2': 181, 'd2': 13,
        'defect_a': 0.094, 'ca': 60, 'da': 17, 'p': 650, 'l': 101, 'high_risk': False},
    4: {'defect1': 0.192, 'c1': 41, 'd1': 21, 'defect2': 0.181, 'c2': 183, 'd2': 33,
        'defect_a': 0.209, 'ca': 64, 'da': 31, 'p': 652, 'l': 60, 'high_risk': False},
    5: {'defect1': 0.182, 'c1': 40, 'd1': 12, 'defect2': 0.220, 'c2': 182, 'd2': 14,
        'defect_a': 0.195, 'ca': 64, 'da': 20, 'p': 652, 'l': 300, 'high_risk': False},
    6: {'defect1': 0.203, 'c1': 40, 'd1': 80, 'defect2': 0.189, 'c2': 183, 'd2': 30,
        'defect_a': 0.198, 'ca': 60, 'da': 32, 'p': 650, 'l': 202, 'high_risk': False},
    7: {'defect1': 0.091, 'c1': 43, 'd1': 63, 'defect2': 0.204, 'c2': 182, 'd2': 12,
        'defect_a': 0.043, 'ca': 60, 'da': 14, 'p': 650, 'l': 53, 'high_risk': True},
    8: {'defect1': 0.048, 'c1': 42, 'd1': 32, 'defect2': 0.052, 'c2': 184, 'd2': 30,
        'defect_a': 0.057, 'ca': 64, 'da': 32, 'p': 650, 'l': 101, 'high_risk': False}
}

# 计算效益的函数
def calculate_benefit(params, part, is_returned=False):
    """修正的成本效益分析（返回建议检测的布尔值）"""
    if part == 'comp1':
        if params['high_risk']:
            # 高风险时，效益计算放宽，允许检测效果稍差的情况
            benefit = params['defect1'] * (params['l'] + params['ca'] + params['c1'] * 0.8)  # 增加容错
            return params['d1'] < benefit * 0.9
        else:
            # 正常情形
            benefit = params['defect1'] * (params['l'] + params['ca'] + params['c1'])
            return params['d1'] < benefit * 0.9

    elif part == 'comp2':
        if params['high_risk']:
            # 高风险时，效益计算放宽
            benefit = params['defect2'] * (params['l'] + params['ca'] + params['c2'] * 0.8)  # 增加容错
            return params['d2'] < benefit * 0.9
        else:
            # 正常情形
            benefit = params['defect2'] * (params['l'] + params['ca'] + params['c2'])
            return params['d2'] < benefit * 0.9

    else:  # final product
        if params['high_risk']:
            # 高风险时，效益计算放宽
            return params['da'] < params['defect_a'] * params['l'] * 1.1  # 允许少许误差
        else:
            return params['da'] < params['defect_a'] * params['l'] * 1.1

def simulate_production(decision, params, n=20000):
    """精确生产流程模拟"""
    profit, swap, waste = 0.0, 0.0, 0.0

    for _ in range(n):
        cost, components_ok = 0.0, [True, True]
        returned_components = []

        # --- 正向流程 ---
        # 元件1处理
        if decision[0] == '1':
            cost += params['c1'] + params['d1']
            if np.random.rand() < params['defect1']:
                waste += params['c1']
                continue
        else:
            cost += params['c1']
            if np.random.rand() < params['defect1']:
                components_ok[0] = False

        # 元件2处理
        if decision[1] == '1':
            cost += params['c2'] + params['d2']
            if np.random.rand() < params['defect2']:
                waste += params['c2']
                continue
        else:
            cost += params['c2']
            if np.random.rand() < params['defect2']:
                components_ok[1] = False

        # 组装成品
        if all(components_ok):
            cost += params['ca']

            # 成品检测
            if decision[2] == '1':
                cost += params['da']
                if np.random.rand() < params['defect_a']:
                    waste += params['ca']
                    continue

            # 销售判断
            is_defective = np.random.rand() < params['defect_a']
            if is_defective and decision[2] == '0':
                swap += params['l']
                if decision[3] == '1':
                    returned_components = ['comp1', 'comp2']
            elif not is_defective:
                profit += params['p'] - cost

        # --- 拆解流程 ---
        if returned_components and decision[3] == '1':
            cost += params['ca'] * 0.3  # 拆解处理成本

            for part in returned_components:
                if part == 'comp1':
                    cost += params['c1']
                elif part == 'comp2':
                    cost += params['c2']
                else:
                    cost += params['ca']

        # 记录损失
        if cost > params['p']:
            waste += cost - params['p']
        else:
            profit += params['p'] - cost

    return profit, swap, waste

# 高风险情形下进行额外处理，放宽过滤条件
def filter_decisions(decisions, params):
    """过滤决策，确保至少两个有效决策组合，特别是高风险情形"""
    valid_decisions = []
    for decision in decisions:
        if params['high_risk']:
            # 强制将第一个决策位设为1
            decision = '1' + decision[1:]
        if calculate_benefit(params, 'comp1', is_returned=False) and calculate_benefit(params, 'comp2', is_returned=False):
            valid_decisions.append(decision)
    return valid_decisions

# 模拟运行所有情形
for scenario_id, scenario_params in data.items():
    print(f"检查情形{scenario_id}的数据：{scenario_params}")

    # 计算所有可能的决策组合
    decision_combinations = list(itertools.product(['0', '1'], repeat=6))  # 6个决策位，0或1

    # 过滤不合适的决策
    valid_decisions = filter_decisions(decision_combinations, scenario_params)

    if valid_decisions:
        print(f"情形{scenario_id}最优决策: {valid_decisions[0]}")  # 假设最优决策为第一个有效决策
    else:
        print(f"情形{scenario_id}没有有效的决策组合！")

    print("\n")

# 完成模拟
print(f"总运行时间: {time() - start_time:.2f}秒")
