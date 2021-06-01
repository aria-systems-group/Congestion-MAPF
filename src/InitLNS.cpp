#include "InitLNS.h"
#include <queue>
#include <algorithm>
#include "GCBS.h"
#include "PBS.h"

InitLNS::InitLNS(const Instance& instance, vector<Agent>& agents, double time_limit, string init_algo_name,
         string replan_algo_name,string init_destory_name, int neighbor_size, int screen) :
         instance(instance), agents(agents), time_limit(time_limit), init_algo_name(init_algo_name),
         replan_algo_name(replan_algo_name), neighbor_size(neighbor_size),
         screen(screen), path_table(instance.map_size, agents.size()), collision_graph(agents.size()),
         replan_time_limit(time_limit)
         {
             if (init_destory_name == "Adaptive")
             {
                 ALNS = true;
                 destroy_weights.assign(INIT_COUNT * num_neighbor_sizes, 1);
             }
             else if (init_destory_name == "Target")
                 init_destroy_strategy = TARGET_BASED;
             else if (init_destory_name == "Collision")
                 init_destroy_strategy = COLLISION_BASED;
             else
             {
                 cerr << "Init Destroy heuristic " << init_destory_name << " does not exists. " << endl;
                 exit(-1);
             }

             goal_table.resize(instance.map_size,-1);

             for (auto& i:agents) {
                 goal_table[i.path_planner.goal_location] = i.id;
             }

         }

bool InitLNS::run()
{
    // only for statistic analysis, and thus is not included in runtime
    sum_of_distances = 0;
    for (const auto & agent : agents)
    {
        sum_of_distances += agent.path_planner.my_heuristic[agent.path_planner.start_location];
    }

    initial_solution_runtime = 0;
    start_time = Time::now();
    bool succ = getInitialSolution();
    initial_solution_runtime = ((fsec)(Time::now() - start_time)).count();
    iteration_stats.emplace_back(neighbor.agents.size(),
            initial_sum_of_costs, initial_solution_runtime, init_algo_name, 0, num_of_colliding_pairs);
    runtime = initial_solution_runtime;
    if (screen >= 3)
        printPath();
    if (screen >= 1)
        cout << "Iteration " << iteration_stats.size() << ", "
             << "group size = " << neighbor.agents.size() << ", "
             << "colliding pairs = " << num_of_colliding_pairs << ", "
             << "solution cost = " << sum_of_costs << ", "
             << "remaining time = " << time_limit - runtime << endl;
    while (runtime < time_limit and num_of_colliding_pairs > 0)
    {
        runtime =((fsec)(Time::now() - start_time)).count();


        if (ALNS)
            chooseDestroyHeuristicbyALNS();

        switch (init_destroy_strategy)
        {
            case TARGET_BASED:
                succ = generateNeighborByTarget();
                break;
            case COLLISION_BASED:
                succ = generateNeighborByCollisionGraph();
                break;
            default:
                cerr << "Wrong neighbor generation strategy" << endl;
                exit(-1);
        }
        if(!succ || neighbor.agents.empty())
            continue;

        // store the neighbor information
        neighbor.old_paths.resize(neighbor.agents.size());
        neighbor.old_sum_of_costs = 0;
        neighbor.old_colliding_pairs.clear();
        for (int i = 0; i < (int)neighbor.agents.size(); i++)
        {
            int a = neighbor.agents[i];
            if (replan_algo_name == "PP" || neighbor.agents.size() == 1)
                neighbor.old_paths[i] = agents[a].path;
            path_table.deletePath(neighbor.agents[i]);
            neighbor.old_sum_of_costs += (int) agents[a].path.size() - 1;
            for (auto j: collision_graph[a])
            {
                neighbor.old_colliding_pairs.emplace(min(a, j), max(a, j));
            }
        }
        if (screen >= 2)
        {
            cout << "Neighbors: ";
            for (auto a : neighbor.agents)
                cout << a << ", ";
            cout << endl;
            cout << "Old colliding pairs (" << neighbor.old_colliding_pairs.size() << "): ";
            for (const auto & p : neighbor.old_colliding_pairs)
            {
                cout << "(" << p.first << "," << p.second << "), ";
            }
            cout << endl;

        }

        if (replan_algo_name == "PP" || neighbor.agents.size() == 1)
            succ = runPP();
        else if (replan_algo_name == "GCBS")
            succ = runGCBS();
        else if (replan_algo_name == "PBS")
            succ = runPBS();
        else
        {
            cerr << "Wrong replanning strategy" << endl;
            exit(-1);
        }

        if (ALNS) // update destroy heuristics
        {
            if (neighbor.colliding_pairs.size() < neighbor.old_colliding_pairs.size())
                destroy_weights[selected_neighbor] =
                        reaction_factor * (neighbor.old_colliding_pairs.size() - neighbor.colliding_pairs.size()) / neighbor.agents.size()
                        + (1 - reaction_factor) * destroy_weights[selected_neighbor];
            else
                destroy_weights[selected_neighbor] =
                        (1 - decay_factor) * destroy_weights[selected_neighbor];
        }
        if (screen >= 2)
            cout << "New colliding pairs = " << neighbor.colliding_pairs.size() << endl;
        if (succ) // update collision graph
        {
            num_of_colliding_pairs += (int)neighbor.colliding_pairs.size() - (int)neighbor.old_colliding_pairs.size();
            for(const auto& agent_pair : neighbor.old_colliding_pairs)
            {
                collision_graph[agent_pair.first].erase(agent_pair.second);
                collision_graph[agent_pair.second].erase(agent_pair.first);
            }
            for(const auto& agent_pair : neighbor.colliding_pairs)
            {
                collision_graph[agent_pair.first].emplace(agent_pair.second);
                collision_graph[agent_pair.second].emplace(agent_pair.first);
            }
            if (screen >= 2)
                printCollisionGraph();
        }
        runtime = ((fsec)(Time::now() - start_time)).count();
        sum_of_costs += neighbor.sum_of_costs - neighbor.old_sum_of_costs;
        if (screen >= 1)
            cout << "Iteration " << iteration_stats.size() << ", "
                 << "group size = " << neighbor.agents.size() << ", "
                 << "colliding pairs = " << num_of_colliding_pairs << ", "
                 << "solution cost = " << sum_of_costs << ", "
                 << "remaining time = " << time_limit - runtime << endl;
        iteration_stats.emplace_back(neighbor.agents.size(), sum_of_costs, runtime, replan_algo_name,
                                     0, num_of_colliding_pairs);
    }


    average_group_size = - iteration_stats.front().num_of_agents;
    for (const auto& data : iteration_stats)
        average_group_size += data.num_of_agents;
    if (average_group_size > 0)
        average_group_size /= (double)(iteration_stats.size() - 1);
    cout << getSolverName() << ": Iterations = " << iteration_stats.size() << ", "
         << "colliding pairs = " << num_of_colliding_pairs << ", "
         << "solution cost = " << sum_of_costs << ", "
         << "initial solution cost = " << initial_sum_of_costs << ", "
         << "runtime = " << runtime << ", "
         << "group size = " << average_group_size << ", "
         << "failed iterations = " << num_of_failures << endl;
    return (num_of_colliding_pairs == 0);
}
bool InitLNS::runGCBS()
{
    vector<SingleAgentSolver*> search_engines;
    search_engines.reserve(neighbor.agents.size());
    for (int i : neighbor.agents)
    {
        search_engines.push_back(&agents[i].path_planner);
    }

    // build path tables
    vector<PathTable> path_tables(neighbor.agents.size(), PathTable(instance.map_size));
    for (int i = 0; i < (int)neighbor.agents.size(); i++)
    {
        int agent_id = neighbor.agents[i];
        for (int j = 0; j < instance.getDefaultNumberOfAgents(); j++)
        {
            if (j != agent_id and collision_graph[agent_id].count(j) == 0)
                path_tables[i].insertPath(j, agents[j].path);
        }
    }

    GCBS gcbs(search_engines, path_tables, screen - 1);
    gcbs.setDisjointSplitting(false);
    gcbs.setBypass(true);
    gcbs.setTargetReasoning(true);

    runtime = ((fsec)(Time::now() - start_time)).count();
    double T = time_limit - runtime;
    if (!iteration_stats.empty()) // replan
        T = min(T, replan_time_limit);
    gcbs.solve(T);
    if (gcbs.best_node->colliding_pairs < (int) neighbor.old_colliding_pairs.size()) // accept new paths
    {
        auto id = neighbor.agents.begin();
        neighbor.colliding_pairs.clear();
        for (size_t i = 0; i < neighbor.agents.size(); i++)
        {
            agents[*id].path = *gcbs.paths[i];
            updateCollidingPairs(neighbor.colliding_pairs, agents[*id].id, agents[*id].path);
            path_table.insertPath(agents[*id].id, agents[*id].path);
            ++id;
        }
        neighbor.sum_of_costs = gcbs.best_node->sum_of_costs;
        return true;
    }
    else // stick to old paths
    {
        if (!neighbor.old_paths.empty())
        {
            for (int id : neighbor.agents)
            {
                path_table.insertPath(agents[id].id, agents[id].path);
            }
            neighbor.sum_of_costs = neighbor.old_sum_of_costs;
        }
        num_of_failures++;
        return false;
    }
}
bool InitLNS::runPBS()
{
    vector<SingleAgentSolver*> search_engines;
    search_engines.reserve(neighbor.agents.size());
    for (int i : neighbor.agents)
    {
        search_engines.push_back(&agents[i].path_planner);
    }

    PBS pbs(search_engines, path_table, screen - 1);
    runtime = ((fsec)(Time::now() - start_time)).count();
    double T = time_limit - runtime;
    if (!iteration_stats.empty()) // replan
        T = min(T, replan_time_limit);
    bool succ = pbs.solve(T, (int)neighbor.agents.size() * 2, neighbor.old_colliding_pairs.size());
    if (succ and pbs.best_node->getCollidingPairs() < (int) neighbor.old_colliding_pairs.size()) // accept new paths
    {
        auto id = neighbor.agents.begin();
        neighbor.colliding_pairs.clear();
        for (size_t i = 0; i < neighbor.agents.size(); i++)
        {
            agents[*id].path = *pbs.paths[i];
            updateCollidingPairs(neighbor.colliding_pairs, agents[*id].id, agents[*id].path);
            path_table.insertPath(agents[*id].id);
            ++id;
        }
        assert(neighbor.colliding_pairs.size() == pbs.best_node->getCollidingPairs());
        neighbor.sum_of_costs = pbs.best_node->sum_of_costs;
        return true;
    }
    else // stick to old paths
    {
        if (!neighbor.old_paths.empty())
        {
            for (int id : neighbor.agents)
            {
                path_table.insertPath(agents[id].id);
            }
            neighbor.sum_of_costs = neighbor.old_sum_of_costs;
        }
        num_of_failures++;
        return false;
    }
}

bool InitLNS::getInitialSolution()
{
    neighbor.agents.resize(agents.size());
    for (int i = 0; i < (int)agents.size(); i++)
        neighbor.agents[i] = i;
    neighbor.old_sum_of_costs = MAX_COST;
    neighbor.sum_of_costs = 0;
    bool succ = false;
    if (init_algo_name == "PP")
        succ = runPP(false);
    else
    {
        cerr <<  "Initial MAPF solver " << init_algo_name << " does not exist!" << endl;
        exit(-1);
    }

    initial_sum_of_costs = neighbor.sum_of_costs;
    sum_of_costs = neighbor.sum_of_costs;
    num_of_colliding_pairs = (int) neighbor.colliding_pairs.size();
    for(const auto& agent_pair : neighbor.colliding_pairs)
    {
        collision_graph[agent_pair.first].emplace(agent_pair.second);
        collision_graph[agent_pair.second].emplace(agent_pair.first);
    }
    if (screen >= 2)
        printCollisionGraph();
    return succ;
}

bool InitLNS::runPP(bool no_wait)
{
    auto shuffled_agents = neighbor.agents;
    std::random_shuffle(shuffled_agents.begin(), shuffled_agents.end());
    if (screen >= 2) {
        cout<<"Neighbors_set: ";
        for (auto id : shuffled_agents)
            cout << id << ", ";
        cout << endl;
    }
    int remaining_agents = (int)shuffled_agents.size();
    auto p = shuffled_agents.begin();
    neighbor.sum_of_costs = 0;
    neighbor.colliding_pairs.clear();
    runtime = ((fsec)(Time::now() - start_time)).count();
    double T = time_limit - runtime; // time limit
    if (!iteration_stats.empty()) // replan
        T = min(T, replan_time_limit);
    auto time = Time::now();
    PathTable dummy_path_table;
    ConstraintTable dummy_constraint_table(dummy_path_table, instance.num_of_cols, instance.map_size, -1);
    while (p != shuffled_agents.end() && ((fsec)(Time::now() - time)).count() < T)
    {
        int id = *p;

        if (no_wait) {
            vector<int> goal_table;
            goal_table.resize(instance.map_size, -1);
            set<int> A_target;
            agents[id].path = agents[id].path_planner.findNoWaitPath(goal_table,A_target);
        }
        else
        {
            dummy_constraint_table.goal_location = agents[id].path_planner.goal_location;
            agents[id].path = agents[id].path_planner.findOptimalPath(dummy_constraint_table, path_table);
        }
        assert(!agents[id].path.empty() && agents[id].path.back().location == agents[id].path_planner.goal_location);
        updateCollidingPairs(neighbor.colliding_pairs, agents[id].id, agents[id].path);
        neighbor.sum_of_costs += (int)agents[id].path.size() - 1;
        remaining_agents--;
        if (screen >= 3)
        {
            runtime = ((fsec)(Time::now() - start_time)).count();
            cout << "After agent " << id << ": Remaining agents = " << remaining_agents <<
                 ", colliding pairs = " << neighbor.colliding_pairs.size() <<
                 ", remaining time = " << time_limit - runtime << " seconds. " << endl;
        }
        if (!neighbor.old_colliding_pairs.empty() && // otherwise it is for the first run
            neighbor.colliding_pairs.size() > neighbor.old_colliding_pairs.size())
            break;
        path_table.insertPath(agents[id].id, agents[id].path);
        ++p;
    }
    if (neighbor.old_colliding_pairs.empty()) // first run
    {
        return (p == shuffled_agents.end() && neighbor.colliding_pairs.empty());
    }
    if (p == shuffled_agents.end() && neighbor.colliding_pairs.size() <= neighbor.old_colliding_pairs.size()) // accept new paths
    {
        return true;
    }
    else // stick to old paths
    {
        if (p != shuffled_agents.end())
            num_of_failures++;
        auto p2 = shuffled_agents.begin();
        while (p2 != p)
        {
            int a = *p2;
            path_table.deletePath(agents[a].id);
            ++p2;
        }
        if (!neighbor.old_paths.empty())
        {
            p2 = neighbor.agents.begin();
            for (int i = 0; i < (int)neighbor.agents.size(); i++)
            {
                int a = *p2;
                agents[a].path = neighbor.old_paths[i];
                path_table.insertPath(agents[a].id, agents[a].path);
                ++p2;
            }
            neighbor.sum_of_costs = neighbor.old_sum_of_costs;
        }
        return false;
    }
}

void InitLNS::updateCollidingPairs(set<pair<int, int>>& colliding_pairs, int agent_id, const Path& path) const
{
    if (path.size() < 2)
        return;
    for (int t = 1; t < (int)path.size(); t++)
    {
        int from = path[t - 1].location;
        int to = path[t].location;
        if ((int)path_table.table[to].size() > t) // vertex conflicts
        {
            for (auto id : path_table.table[to][t])
            {
                colliding_pairs.emplace(min(agent_id, id), max(agent_id, id));
            }
        }
        if (from != to && path_table.table[to].size() >= t && path_table.table[from].size() > t) // edge conflicts
        {
            for (auto a1 : path_table.table[to][t - 1])
            {
                for (auto a2: path_table.table[from][t])
                {
                    if (a1 == a2)
                        colliding_pairs.emplace(min(agent_id, a1), max(agent_id, a1));
                }
            }
        }
        //auto id = getAgentWithTarget(to, t);
        //if (id >= 0) // this agent traverses the target of another agent
        //    colliding_pairs.emplace(min(agent_id, id), max(agent_id, id));
        if (!path_table.goals.empty() && path_table.goals[to] < t) // target conflicts
        { // this agent traverses the target of another agent
            for (auto id : path_table.table[to][path_table.goals[to]]) // look at all agents at the goal time
            {
                if (agents[id].path.back().location == to) // if agent id's goal is to, then this is the agent we want
                {
                    colliding_pairs.emplace(min(agent_id, id), max(agent_id, id));
                    break;
                }
            }
        }
    }
    int goal = path.back().location; // target conflicts - some other agent traverses the target of this agent
    for (int t = (int)path.size(); t < path_table.table[goal].size(); t++)
    {
        for (auto id : path_table.table[goal][t])
            colliding_pairs.emplace(min(agent_id, id), max(agent_id, id));
    }
}

void InitLNS::chooseDestroyHeuristicbyALNS()
{
    double sum = 0;
    for (const auto& h : destroy_weights)
        sum += h;
    if (screen >= 2)
    {
        cout << "destroy weights = ";
        for (const auto& h : destroy_weights)
            cout << h / sum << ",";
    }
    double r = (double) rand() / RAND_MAX;
    double threshold = destroy_weights[0];
    selected_neighbor = 0;
    while (threshold < r * sum)
    {
        selected_neighbor++;
        threshold += destroy_weights[selected_neighbor];
    }
    switch (selected_neighbor / num_neighbor_sizes)
    {
        case 0 : init_destroy_strategy = TARGET_BASED; break;
        case 1 : init_destroy_strategy = COLLISION_BASED; break;
        default : cerr << "ERROR" << endl; exit(-1);
    }
    // neighbor_size = (int) pow(2, selected_neighbor % num_neighbor_sizes + 1);
}

bool InitLNS::generateNeighborByCollisionGraph()
{
    /*unordered_map<int, list<int>> G;
    for (int i = 0; i < (int)collision_graph.size(); i++)
    {
        if (!collision_graph[i].empty())
            G[i].assign(collision_graph[i].begin(), collision_graph[i].end());
    }
    assert(!G.empty());
    assert(neighbor_size <= (int)agents.size());
    set<int> neighbors_set;
    if ((int)G.size() < neighbor_size)
    {
        for (const auto& node : G)
            neighbors_set.insert(node.first);
        int count = 0;
        while ((int)neighbors_set.size() < neighbor_size && count < 10)
        {
            int a1 = *std::next(neighbors_set.begin(), rand() % neighbors_set.size());
            int a2 = randomWalk(a1);
            if (a2 != NO_AGENT)
                neighbors_set.insert(a2);
            else
                count++;
        }
    }
    else
    {
        int a = -1;
        while ((int)neighbors_set.size() < neighbor_size)
        {
            if (a == -1)
            {
                a = std::next(G.begin(), rand() % G.size())->first;
                neighbors_set.insert(a);
            }
            else
            {
                a = *std::next(G[a].begin(), rand() % G[a].size());
                auto ret = neighbors_set.insert(a);
                if (!ret.second) // no new element inserted
                    a = -1;
            }
        }
    }
    neighbor.agents.assign(neighbors_set.begin(), neighbors_set.end());
    if (screen >= 2)
        cout << "Generate " << neighbor.agents.size() << " neighbors by collision graph" << endl;
    return true;*/

    vector<int> all_vertices;
    all_vertices.reserve(collision_graph.size());
    for (int i = 0; i < (int)collision_graph.size(); i++)
    {
        if (!collision_graph[i].empty())
            all_vertices.push_back(i);
    }
    unordered_map<int, set<int>> G;
    auto v = all_vertices[rand() % all_vertices.size()]; // pick a random vertex
    findConnectedComponent(collision_graph, v, G);
    assert(G.size() > 1);

    assert(neighbor_size <= (int)agents.size());
    set<int> neighbors_set;
    if ((int)G.size() <= neighbor_size)
    {
        for (const auto& node : G)
            neighbors_set.insert(node.first);
        int count = 0;
        while ((int)neighbors_set.size() < neighbor_size && count < 10)
        {
            int a1 = *std::next(neighbors_set.begin(), rand() % neighbors_set.size());
            int a2 = randomWalk(a1);
            if (a2 != NO_AGENT)
                neighbors_set.insert(a2);
            else
                count++;
        }
    }
    else
    {
        int a = std::next(G.begin(), rand() % G.size())->first;
        neighbors_set.insert(a);
        while ((int)neighbors_set.size() < neighbor_size)
        {
            a = *std::next(G[a].begin(), rand() % G[a].size());
            neighbors_set.insert(a);
        }
    }
    neighbor.agents.assign(neighbors_set.begin(), neighbors_set.end());
    if (screen >= 2)
        cout << "Generate " << neighbor.agents.size() << " neighbors by collision graph" << endl;
    return true;

}

bool InitLNS::generateNeighborByTarget()
{
    int a = 0;
    int a_num_collisions= 0;
    for (int i = 0 ; i<collision_graph.size();i++){
        if (collision_graph[i].size()> a_num_collisions){
            a = i;
            a_num_collisions = collision_graph[i].size();
        }
    }
    set<pair<int,int>> A_start; // an ordered set of (time, id) pair.
    set<int> A_target;


    for(int t = 0 ;t< path_table.table[agents[a].path_planner.start_location].size();t++){
        for(auto id : path_table.table[agents[a].path_planner.start_location][t]){
            if (id!=a)
                A_start.insert(make_pair(t,id));
        }
    }



    Path path = agents[a].path_planner.findNoWaitPath(goal_table,A_target);// generate non-wait path and collect A_target
    assert(!path.empty());


    if (screen >= 3){
        cout<<"     Find path with length: "<<path.size()<<endl;
        cout<<"     Selected a : "<< a<<endl;
        cout<<"     Select A_start: ";
        for(auto e: A_start)
            cout<<"("<<e.first<<","<<e.second<<"), ";
        cout<<endl;
        cout<<"     Select A_target: ";
        for(auto e: A_target)
            cout<<e<<", ";
        cout<<endl;
    }

    set<int> neighbors_set;

    neighbors_set.insert(a);

    if(A_start.size() + A_target.size() >= neighbor_size-1){
        if (A_start.empty()){
            vector<int> shuffled_agents;
            shuffled_agents.assign(A_target.begin(),A_target.end());
            std::random_shuffle(shuffled_agents.begin(), shuffled_agents.end());
            neighbors_set.insert(shuffled_agents.begin(), shuffled_agents.begin() + neighbor_size-1);
        }
        else if (A_target.size() >= neighbor_size){
            vector<int> shuffled_agents;
            shuffled_agents.assign(A_target.begin(),A_target.end());
            std::random_shuffle(shuffled_agents.begin(), shuffled_agents.end());
            neighbors_set.insert(shuffled_agents.begin(), shuffled_agents.begin() + neighbor_size-2);

            neighbors_set.insert(A_start.begin()->second);
        }
        else{
            neighbors_set.insert(A_target.begin(), A_target.end());
            for(auto e : A_start){
                //A_start is ordered by time.
                if (neighbors_set.size()>= neighbor_size)
                    break;
                neighbors_set.insert(e.second);

            }
        }
    }
    else if (!A_start.empty() || !A_target.empty()){
        neighbors_set.insert(A_target.begin(), A_target.end());
        for(auto e : A_start){
            neighbors_set.insert(e.second);
        }

        set<int> tabu_set;
        while(neighbors_set.size()<neighbor_size){
            int rand_int = rand() % neighbors_set.size();
            auto it = neighbors_set.begin();
            std::advance(it, rand_int);
            a = *it;
            tabu_set.insert(a);

            if(tabu_set.size() == neighbors_set.size())
                break;

            vector<int> targets;
            for(auto p: agents[a].path){
                if(goal_table[p.location]>-1){
                    targets.push_back(goal_table[p.location]);
                }
            }

            if(targets.empty())
                continue;
            rand_int = rand() %targets.size();
            neighbors_set.insert(*(targets.begin()+rand_int));
        }
    }



    neighbor.agents.assign(neighbors_set.begin(), neighbors_set.end());
    if (screen >= 2)
        cout << "Generate " << neighbor.agents.size() << " neighbors by target" << endl;
    return true;
}

// Random walk; return the first agent that the agent collides with
int InitLNS::randomWalk(int agent_id)
{
    int t = rand() % agents[agent_id].path.size();
    int loc = agents[agent_id].path[t].location;
    while (t <= path_table.makespan and
           (path_table.table[loc].size() <= t or
           path_table.table[loc][t].empty() or
           (path_table.table[loc][t].size() == 1 and path_table.table[loc][t].front() == agent_id)))
    {
        auto next_locs = instance.getNeighbors(loc);
        next_locs.push_back(loc);
        int step = rand() % next_locs.size();
        auto it = next_locs.begin();
        loc = *std::next(next_locs.begin(), rand() % next_locs.size());
        t = t + 1;
    }
    if (t > path_table.makespan)
        return NO_AGENT;
    else
        return *std::next(path_table.table[loc][t].begin(), rand() % path_table.table[loc][t].size());
}

void InitLNS::validateSolution() const
{
    int sum = 0;
    for (const auto& a1_ : agents)
    {
        if (a1_.path.empty())
        {
            cerr << "No solution for agent " << a1_.id << endl;
            exit(-1);
        }
        else if (a1_.path_planner.start_location != a1_.path.front().location)
        {
            cerr << "The path of agent " << a1_.id << " starts from location " << a1_.path.front().location
                << ", which is different from its start location " << a1_.path_planner.start_location << endl;
            exit(-1);
        }
        else if (a1_.path_planner.goal_location != a1_.path.back().location)
        {
            cerr << "The path of agent " << a1_.id << " ends at location " << a1_.path.back().location
                 << ", which is different from its goal location " << a1_.path_planner.goal_location << endl;
            exit(-1);
        }
        for (int t = 1; t < (int) a1_.path.size(); t++ )
        {
            if (!instance.validMove(a1_.path[t - 1].location, a1_.path[t].location))
            {
                cerr << "The path of agent " << a1_.id << " jump from "
                     << a1_.path[t - 1].location << " to " << a1_.path[t].location
                     << " between timesteps " << t - 1 << " and " << t << endl;
                exit(-1);
            }
        }
        sum += (int) a1_.path.size() - 1;
        for (const auto& a2_: agents)
        {
            if (a1_.id >= a2_.id || a1_.path.empty())
                continue;
            const auto a1 = a1_.path.size() <= a2_.path.size()? a1_ : a2_;
            const auto a2 = a1_.path.size() <= a2_.path.size()? a2_ : a1_;
            int t = 1;
            for (; t < (int) a1.path.size(); t++)
            {
                if (a1.path[t].location == a2.path[t].location) // vertex conflict
                {
                    cerr << "Find a vertex conflict between agents " << a1.id << " and " << a2.id <<
                            " at location " << a1.path[t].location << " at timestep " << t << endl;
                    exit(-1);
                }
                else if (a1.path[t].location == a2.path[t - 1].location &&
                        a1.path[t - 1].location == a2.path[t].location) // edge conflict
                {
                    cerr << "Find an edge conflict between agents " << a1.id << " and " << a2.id <<
                         " at edge (" << a1.path[t - 1].location << "," << a1.path[t].location <<
                         ") at timestep " << t << endl;
                    exit(-1);
                }
            }
            int target = a1.path.back().location;
            for (; t < (int) a2.path.size(); t++)
            {
                if (a2.path[t].location == target)  // target conflict
                {
                    cerr << "Find a target conflict where agent " << a2.id << " (of length " << a2.path.size() - 1<<
                         ") traverses agent " << a1.id << " (of length " << a1.path.size() - 1<<
                         ")'s target location " << target << " at timestep " << t << endl;
                    exit(-1);
                }
            }
        }
    }
    if (sum_of_costs != sum)
    {
        cerr << "The computed sum of costs " << sum_of_costs <<
             " is different from the sum of the paths in the solution " << sum << endl;
        exit(-1);
    }
}

void InitLNS::writeIterStatsToFile(string file_name) const
{
    std::ofstream output;
    output.open(file_name);
    // header
    output << "num of agents," <<
           "sum of costs," <<
           "num of colliding pairs," <<
           "runtime," <<
           "cost lowerbound," <<
           "sum of distances," <<
           "MAPF algorithm" << endl;

    for (const auto &data : iteration_stats)
    {
        output << data.num_of_agents << "," <<
               data.sum_of_costs << "," <<
               data.num_of_colliding_pairs << "," <<
               data.runtime << "," <<
               max(sum_of_costs_lowerbound, sum_of_distances) << "," <<
               sum_of_distances << "," <<
               data.algorithm << endl;
    }
    output.close();
}

void InitLNS::writeResultToFile(string file_name) const
{
    std::ifstream infile(file_name);
    bool exist = infile.good();
    infile.close();
    if (!exist)
    {
        ofstream addHeads(file_name);
        addHeads << "runtime,solution cost,initial solution cost,min f value,root g value," <<
                 "iterations," <<
                 "group size," <<
                 "runtime of initial solution,area under curve," <<
                 "solver name,instance name" << endl;
        addHeads.close();
    }
    ofstream stats(file_name, std::ios::app);
    double auc = 0;
    if (!iteration_stats.empty())
    {
        auto prev = iteration_stats.begin();
        auto curr = prev;
        ++curr;
        while (curr != iteration_stats.end() && curr->runtime < time_limit)
        {
            auc += (prev->sum_of_costs - sum_of_distances) * (curr->runtime - prev->runtime);
            prev = curr;
            ++curr;
        }
        auc += (prev->sum_of_costs - sum_of_distances) * (time_limit - prev->runtime);
    }
    stats << runtime << "," << sum_of_costs << "," << initial_sum_of_costs << "," <<
            max(sum_of_distances, sum_of_costs_lowerbound) << "," << sum_of_distances << "," <<
            iteration_stats.size() << "," << average_group_size << "," <<
            initial_solution_runtime << "," << auc << "," <<
            getSolverName() << "," << instance.getInstanceName() << endl;
    stats.close();
}

void InitLNS::writePathsToFile(string file_name) const
{
    std::ofstream output;
    output.open(file_name);
    // header
    // output << agents.size() << endl;

    for (const auto &agent : agents)
    {
        output << "Agent " << agent.id << ":";
        for (const auto &state : agent.path)
            output << "(" << instance.getRowCoordinate(state.location) << "," <<
                            instance.getColCoordinate(state.location) << ")->";
        output << endl;
    }
    output.close();
}

void InitLNS::printCollisionGraph() const
{
    cout << "Collision graph: ";
    int edges = 0;
    for (size_t i = 0; i < collision_graph.size(); i++)
    {
        for (int j : collision_graph[i])
        {
            if (i < j)
            {
                cout << "(" << i << "," << j << "),";
                edges++;
            }
        }
    }
    cout << endl <<  "|V|=" << collision_graph.size() << ", |E|=" << edges << endl;
}


unordered_map<int, set<int>>& InitLNS::findConnectedComponent(const vector<set<int>>& graph, int vertex,
                                                               unordered_map<int, set<int>>& sub_graph)
{
    std::queue<int> Q;
    Q.push(vertex);
    sub_graph.emplace(vertex, graph[vertex]);
    while (!Q.empty())
    {
        auto v = Q.front(); Q.pop();
        for (const auto & u : graph[v])
        {
            auto ret = sub_graph.emplace(u, graph[u]);
            if (ret.second) // insert successfully
                Q.push(u);
        }
    }
    return sub_graph;
}

void InitLNS::printPath() const
{
    for (const auto& agent : agents)
        cout << "Agent " << agent.id << ": " << agent.path << endl;
}