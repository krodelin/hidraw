defmodule Hidraw do
  use GenServer

  def start_link(fd) do
    GenServer.start_link(__MODULE__, [fd, self()])
  end

  def enumerate() do
    executable = :code.priv_dir(:hidraw) ++ '/ex_hidraw'

    port =
      Port.open({:spawn_executable, executable}, [
        {:args, ["enumerate"]},
        {:packet, 2},
        :use_stdio,
        :binary
      ])

    receive do
      {^port, {:data, <<?r, message::binary>>}} ->
        :erlang.binary_to_term(message)
    after
      5_000 ->
        Port.close(port)
        []
    end
  end

  def report_descriptor(pid, timeout \\ 5000) do
    GenServer.call(pid, :report_desc, timeout)
  end

  def output(pid, data) do
    GenServer.cast(pid, {:output, data})
  end

  def init([fd, caller]) do
    executable = :code.priv_dir(:hidraw) ++ '/ex_hidraw'

    port =
      Port.open({:spawn_executable, executable}, [
        {:args, [fd]},
        {:packet, 2},
        :use_stdio,
        :binary,
        :exit_status
      ])

    state = %{port: port, name: fd, callback: caller, buffer: [], report_desc: nil}

    {:ok, state}
  end

  def handle_call(:report_desc, _from, s) do
    {:reply, {:ok, s.report_desc}, s}
  end

  def handle_cast({:output, data},  %{port: port} = s) do
    send(port, {self(), {:command, data}})
    {:noreply, s}
  end

  def handle_info({_, {:data, <<?n, message::binary>>}}, state) do
    msg = :erlang.binary_to_term(message)
    handle_port(msg, state)
  end

  def handle_info({_, {:data, <<?d, message::binary>>}}, state) do
    {:descriptor, descriptor} = :erlang.binary_to_term(message)
    send(state.callback, {:hidraw, state.name, {:report_descriptor, descriptor}})
    {:noreply, %{state | report_desc: descriptor}}
  end

  def handle_info({_, {:data, <<?e, message::binary>>}}, state) do
    error = :erlang.binary_to_term(message)
    send(state.callback, {:hidraw, state.name, error})
    {:stop, error, state}
  end

  def handle_info({_, {:exit_status, status}}, state) do
    send(state.callback, {:hidraw, state.name, {:exit, status}})
    {:stop, {:exit, status}, state}
  end

  defp handle_port({:data, value}, state) do
    send(state.callback, {:hidraw, state.name, value})
    {:noreply, state}
  end
end
