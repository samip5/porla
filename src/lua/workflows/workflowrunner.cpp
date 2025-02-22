#include "workflowrunner.hpp"

#include <boost/log/trivial.hpp>

#include "actionbuilder.hpp"
#include "actions/function.hpp"

using porla::Lua::Workflows::ActionBuilder;
using porla::Lua::Workflows::Actions::Function;
using porla::Lua::Workflows::WorkflowRunner;

WorkflowRunner::WorkflowRunner(const WorkflowRunnerOptions& opts, sol::table ctx, std::vector<sol::object> actions)
    : m_opts(opts)
    , m_ctx(std::move(ctx))
    , m_actions(std::move(actions))
    , m_current_action_index(0)
    , m_running(false)
{
}

WorkflowRunner::~WorkflowRunner()
{
    m_on_finished();
}

boost::signals2::connection WorkflowRunner::OnFinished(const VoidSignal::slot_type &subscriber)
{
    return m_on_finished.connect(subscriber);
}

void WorkflowRunner::Complete()
{
    Complete({});
}

void WorkflowRunner::Complete(sol::object output)
{
    // Set output
    std::vector<sol::object>& actions = m_ctx["actions"];
    actions.push_back(output);

    m_current_action_index++;

    boost::asio::post(
        m_opts.io,
        [_this = shared_from_this()]()
        {
            _this->RunOne();
        });
}

void WorkflowRunner::Run()
{
    if (m_running) return;
    m_running = true;

    boost::asio::post(
        m_opts.io,
        [_this = shared_from_this()]()
        {
            _this->RunOne();
        });
}

void WorkflowRunner::RunOne()
{
    if (m_actions.empty())                          return;
    if (m_current_action_index >= m_actions.size()) return;

    const auto& current_action = m_actions.at(m_current_action_index);

    BOOST_LOG_TRIVIAL(debug) << "Running action " << m_current_action_index + 1 << " of " << m_actions.size();

    if (current_action.is<ActionBuilder*>())
    {
        const ActionBuilderOptions opts = {
            .io      = m_opts.io,
            .session = m_opts.session
        };

        try
        {
            auto instance = current_action.as<ActionBuilder*>()->Build(opts);

            porla::Lua::Workflows::ActionParams params{
                .context = m_ctx,
                .state   = m_opts.lua
            };

            instance->Invoke(params, shared_from_this());
        }
        catch (const std::exception& ex)
        {
            BOOST_LOG_TRIVIAL(error) << "Error when invoking action: " << ex.what();
        }
    }
    else if (current_action.is<sol::function>())
    {
        try
        {
            auto tbl = m_opts.lua.create_table();
            tbl["function"] = current_action;

            auto instance = std::make_shared<Function>(tbl);

            porla::Lua::Workflows::ActionParams params{
                .context = m_ctx,
                .state   = m_opts.lua
            };

            instance->Invoke(params, shared_from_this());
        }
        catch (const std::exception& ex)
        {
            BOOST_LOG_TRIVIAL(error) << "Error when invoking free function: " << ex.what();
        }
    }
}
